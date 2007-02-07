/* This file is part of FlowCanvas.  Copyright (C) 2005 Dave Robillard.
 * 
 * FlowCanvas is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 * 
 * FlowCanvas is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "FlowCanvas.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <iostream>
#include <cmath>
#include <boost/enable_shared_from_this.hpp>
#include "Port.h"
#include "Module.h"

using std::cerr; using std::cout; using std::endl;

namespace LibFlowCanvas {
	

FlowCanvas::FlowCanvas(double width, double height)
: _zoom(1.0),
  _width(width),
  _height(height),
  _drag_state(NOT_DRAGGING),
  _remove_objects(true),
  _base_rect(*root(), 0, 0, width, height),
  _select_rect(NULL),
  _select_dash(NULL)
{
	set_scroll_region(0.0, 0.0, width, height);
	set_center_scroll_region(true);
	
	_base_rect.property_fill_color_rgba() = 0x000000FF;
	//_base_rect.show();
	//_base_rect.signal_event().connect(sigc::mem_fun(this, &FlowCanvas::scroll_drag_handler));
	_base_rect.signal_event().connect(sigc::mem_fun(this, &FlowCanvas::canvas_event));
	_base_rect.signal_event().connect(sigc::mem_fun(this, &FlowCanvas::select_drag_handler));
	_base_rect.signal_event().connect(sigc::mem_fun(this, &FlowCanvas::connection_drag_handler));
	
	set_dither(Gdk::RGB_DITHER_NORMAL); // NONE or NORMAL or MAX
	
	// Dash style for selected modules and selection box
	_select_dash = new ArtVpathDash();
	_select_dash->n_dash = 2;
	_select_dash->dash = art_new(double, 2);
	_select_dash->dash[0] = 5;
	_select_dash->dash[1] = 5;
		
	Glib::signal_timeout().connect(
		sigc::mem_fun(this, &FlowCanvas::animate_selected), 300);
}


FlowCanvas::~FlowCanvas()
{
	destroy();
}


void
FlowCanvas::set_zoom(double pix_per_unit)
{
	if (_zoom == pix_per_unit)
		return;

	_zoom = pix_per_unit;
	set_pixels_per_unit(_zoom);

	for (ModuleMap::iterator m = _modules.begin(); m != _modules.end(); ++m)
		(*m).second->zoom(_zoom);
}


void
FlowCanvas::zoom_full()
{
	int win_width, win_height;
	Glib::RefPtr<Gdk::Window> win = get_window();
	win->get_size(win_width, win_height);

	// Box containing all canvas items
	double left   = DBL_MAX;
	double right  = DBL_MIN;
	double top    = DBL_MIN;
	double bottom = DBL_MAX;

	for (ModuleMap::iterator m = _modules.begin(); m != _modules.end(); ++m) {
		boost::shared_ptr<Module> mod = (*m).second;
		if (!mod)
			continue;
		
		if (mod->property_x() < left)
			left = mod->property_x();
		if (mod->property_x() + mod->width() > right)
			right = mod->property_x() + mod->width();
		if (mod->property_y() < bottom)
			bottom = mod->property_y();
		if (mod->property_y() + mod->height() > top)
			top = mod->property_y() + mod->height();
	}

	static const double pad = 4.0;

	const double new_zoom = std::min(
		((double)win_width / (double)(right - left + pad*2.0)),
		((double)win_height / (double)(top - bottom + pad*2.0)));

	set_zoom(new_zoom);
	
	int scroll_x, scroll_y;
	w2c(lrintf(left - pad), lrintf(bottom - pad), scroll_x, scroll_y);

	scroll_to(scroll_x, scroll_y);

}


void
FlowCanvas::clear_selection()
{
	unselect_ports();

	for (list<boost::shared_ptr<Module> >::iterator m = _selected_modules.begin(); m != _selected_modules.end(); ++m)
		(*m)->set_selected(false);
	
	for (list<boost::shared_ptr<Connection> >::iterator c = _selected_connections.begin(); c != _selected_connections.end(); ++c)
		(*c)->set_selected(false);

	_selected_modules.clear();
	_selected_connections.clear();
}


void
FlowCanvas::unselect_connection(Connection* connection)
{
	for (ConnectionList::iterator i = _selected_connections.begin(); i != _selected_connections.end();) {
		if ((*i).get() == connection) {
			i = _selected_connections.erase(i);
		} else {
			++i;
		}
	}

	connection->set_selected(false);
}


void
FlowCanvas::select_module(const string& name)
{
	boost::shared_ptr<Module> m = get_module(name);
	if (m)
		select_module(m);
}


/** Add a module to the current selection, and automagically select any connections
 * between selected modules */
void
FlowCanvas::select_module(boost::shared_ptr<Module> m)
{
	assert(! m->selected());
	
	_selected_modules.push_back(m);

	for (ConnectionList::iterator i = _connections.begin(); i != _connections.end(); ++i) {
		const boost::shared_ptr<Connection> c = (*i);
		const boost::shared_ptr<Port> source = c->source().lock();
		const boost::shared_ptr<Port> dest = c->dest().lock();
		if (!source || !dest)
			continue;

		const boost::shared_ptr<Module> source_module = source->module().lock();
		const boost::shared_ptr<Module> dest_module = dest->module().lock();
		if (!source_module || !dest_module)
			continue;

		if ( !c->selected()) {
			if (source_module == m && dest_module->selected()) {
				c->set_selected(true);
				_selected_connections.push_back(c);
			} else if (dest_module == m && source_module->selected()) {
				c->set_selected(true);
				_selected_connections.push_back(c);
			} 
		}
	}
				
	m->set_selected(true);
}


void
FlowCanvas::unselect_module(boost::shared_ptr<Module> m)
{

	// Remove any connections that aren't selected anymore because this module isn't
	cerr << "FIXME: remove connection selection\n";
#if 0
	boost::shared_ptr<Connection> c;
	for (ConnectionList::iterator i = _selected_connections.begin(); i != _selected_connections.end();) {
		c = (*i);
		if (c->selected()
			&& ((c->source()->module() == m && c->dest()->module()->selected())
				|| c->dest()->module() == m && c->source()->module()->selected()))
			{
				c->selected(false);
				i = _selected_connections.erase(i);
		} else {
			++i;
		}
	}
#endif
	// Remove the module
	for (list<boost::shared_ptr<Module> >::iterator i = _selected_modules.begin(); i != _selected_modules.end(); ++i) {
		if ((*i) == m) {
			_selected_modules.erase(i);
			break;
		}
	}
	
	m->set_selected(false);
}


void
FlowCanvas::unselect_module(const string& name)
{
	boost::shared_ptr<Module> m = get_module(name);
	if (m)
		unselect_module(m);
}


/** Removes all ports and connections and modules.
 */
void
FlowCanvas::destroy()
{
	_remove_objects = false;

	clear_selection();

	_connections.clear();
	_modules.clear();

	_selected_port.reset();
	_connect_port.reset();

	_remove_objects = true;
}


void
FlowCanvas::unselect_ports()
{
	if (_selected_port)
		_selected_port->set_fill_color(_selected_port->color()); // deselect the old one
	
	_selected_port.reset();
}


void
FlowCanvas::selected_port(boost::shared_ptr<Port> p)
{
	unselect_ports();
	
	_selected_port = p;
	
	if (p)
		p->set_fill_color(0xFF0000FF);
}


boost::shared_ptr<Module>
FlowCanvas::get_module(const string& name)
{
	ModuleMap::iterator m = _modules.find(name);

	return (m == _modules.end()) ? boost::shared_ptr<Module>() : (*m).second;
}


/** Sets the passed module's location to a reasonable default.
 */
void
FlowCanvas::set_default_placement(boost::shared_ptr<Module> m)
{
	assert(m);
	
	// Simple cascade.  This will get more clever in the future.
	double x = ((_width / 2.0) + (_modules.size() * 25));
	double y = ((_height / 2.0) + (_modules.size() * 25));

	m->move_to(x, y);
}


void
FlowCanvas::add_module(boost::shared_ptr<Module> m)
{
	assert(m);
	std::pair<string, boost::shared_ptr<Module> > p(m->name(), m);
	_modules.insert(p);
	m->show();
}


/** Remove a module from the canvas, cutting all references.
 *
 * The removed Module is returned, or NULL if not found.
 */
boost::shared_ptr<Module>
FlowCanvas::remove_module(const string& name)
{
	boost::shared_ptr<Module> ret;

	if (!_remove_objects)
		return ret;

	ModuleMap::iterator m = _modules.find(name);
	if (m != _modules.end()) {
		ret = m->second;
		_modules.erase(m);
	}
	
	return ret;
}


boost::shared_ptr<Port>
FlowCanvas::get_port(const string& node_name, const string& port_name)
{
	for (ModuleMap::iterator i = _modules.begin(); i != _modules.end(); ++i) {
		const boost::shared_ptr<Module> module = (*i).second;
		const boost::shared_ptr<Port>   port = module->get_port(port_name);
		if (module->name() == node_name && port)
			return port;
	}
	
	return boost::shared_ptr<Port>();
}


bool
FlowCanvas::rename_module(const string& old_name, const string& current_name)
{
	for (ModuleMap::iterator i = _modules.begin(); i != _modules.end(); ++i) {
		const boost::shared_ptr<Module> module = (*i).second;
		if (!module)
			continue;
		
		assert(module->name() != old_name);
		
		if ((*i).first == old_name) {
			assert(module->name() == current_name);
			_modules.erase(i);
			add_module(module);
			return true;
		}
	}

	return false;
}


boost::shared_ptr<Connection>
FlowCanvas::remove_connection(boost::shared_ptr<Port> port1, boost::shared_ptr<Port> port2)
{
	boost::shared_ptr<Connection> ret;

	if (!_remove_objects)
		return ret;

	assert(port1);
	assert(port2);
	
	boost::shared_ptr<Connection> c = get_connection(port1, port2);
	if (!c) {
		cerr << "Couldn't find connection.\n";
		return ret;
	} else {
		remove_connection(c);
		return c;
	}
}


bool
FlowCanvas::are_connected(boost::shared_ptr<const Port> port1, boost::shared_ptr<const Port> port2)
{
	assert(port1);
	assert(port2);
	
	ConnectionList::const_iterator c;

	for (c = _connections.begin(); c != _connections.end(); ++c) {
		const boost::shared_ptr<Port> src = (*c)->source().lock();
		const boost::shared_ptr<Port> dst = (*c)->dest().lock();
		if (!src || !dst)
			continue;

		if ( (src == port1 && dst == port2) || (dst == port1 && src == port2) )
			return true;
	}

	return false;
}


boost::shared_ptr<Connection>
FlowCanvas::get_connection(boost::shared_ptr<Port> port1, boost::shared_ptr<Port> port2)
{
	assert(port1);
	assert(port2);
	
	for (ConnectionList::iterator i = _connections.begin(); i != _connections.end(); ++i) {
		const boost::shared_ptr<Port> src = (*i)->source().lock();
		const boost::shared_ptr<Port> dst = (*i)->dest().lock();
		if (!src || !dst)
			continue;

		if ( (src == port1 && dst == port2) || (dst == port1 && src == port2) )
			return *i;
	}
	
	return boost::shared_ptr<Connection>();
}


bool
FlowCanvas::add_connection(boost::shared_ptr<Port> port1, boost::shared_ptr<Port> port2)
{
	/*if (port1->module()->canvas() != this
		|| port2->module()->canvas() != this
		|| port1->is_input() == port2->is_input()
		|| port1->is_output() == port2->is_output()) {
		return false;
	}*/

	boost::shared_ptr<Port> src_port;
	boost::shared_ptr<Port> dst_port;
	if (port1->is_output()) {
		assert(port2->is_input());
		src_port = port1;
		dst_port = port2;
	} else {
		assert(port2->is_output());
		src_port = port2;
		dst_port = port1;
	}

	// Create (graphical) connection object
	if ( ! get_connection(src_port, dst_port)) {
		boost::shared_ptr<Connection> c(new Connection(shared_from_this(), src_port, dst_port));
		port1->add_connection(c);
		port2->add_connection(c);
		_connections.push_back(c);
	}

	return true;
}


bool
FlowCanvas::add_connection(boost::shared_ptr<Connection> c)
{
	const boost::shared_ptr<Port> src = c->source().lock();
	const boost::shared_ptr<Port> dst = c->dest().lock();

	if (src && dst) {
		src->add_connection(c);
		dst->add_connection(c);
		_connections.push_back(c);
		return true;
	} else {
		return false;
	}
}


void
FlowCanvas::remove_connection(boost::shared_ptr<Connection> connection)
{
	if (!_remove_objects)
		return;

	ConnectionList::iterator i = find(_connections.begin(), _connections.end(), connection);

	if (i != _connections.end()) {
		const boost::shared_ptr<Connection> c = *i;
		
		const boost::shared_ptr<Port> src = c->source().lock();
		const boost::shared_ptr<Port> dst = c->dest().lock();

		if (src)
			src->remove_connection(c);

		if (dst)
			dst->remove_connection(c);
		
		_connections.erase(i);
	}
}


void
FlowCanvas::flag_all_connections()
{
	for (ConnectionList::iterator c = _connections.begin(); c != _connections.end(); ++c)
		(*c)->set_flagged(true);
}


void
FlowCanvas::destroy_all_flagged_connections()
{
	_remove_objects = false;

	for (ConnectionList::iterator c = _connections.begin(); c != _connections.end() ; ) {
		if ((*c)->flagged()) {
			ConnectionList::iterator next = c;
			++next;
			const boost::shared_ptr<Port> src = (*c)->source().lock();
			const boost::shared_ptr<Port> dst = (*c)->dest().lock();
			if (src)
				src->remove_connection(*c);
			if (dst)
				dst->remove_connection(*c);
			_connections.erase(c);
			c = next;
		} else {
			++c;
		}
	}

	_remove_objects = true;
}


void
FlowCanvas::destroy_all_connections()
{
	_remove_objects = false;

	for (ConnectionList::iterator c = _connections.begin(); c != _connections.end(); ++c) {
		const boost::shared_ptr<Port> src = (*c)->source().lock();
		const boost::shared_ptr<Port> dst = (*c)->dest().lock();
		if (src)
			src->remove_connection(*c);
		if (dst)
			dst->remove_connection(*c);
	}

	_connections.clear();

	_remove_objects = true;
}


/** Called when two ports are 'toggled' (connected or disconnected)
 */
void
FlowCanvas::ports_joined(boost::shared_ptr<Port> port1, boost::shared_ptr<Port> port2)
{
	assert(port1);
	assert(port2);

	port1->set_highlighted(false);
	port2->set_highlighted(false);

	string src_mod_name, dst_mod_name, src_port_name, dst_port_name;

	boost::shared_ptr<Port> src_port;
	boost::shared_ptr<Port> dst_port;
	
	if (port2->is_input() && ! port1->is_input()) {
		src_port = port1;
		dst_port = port2;
	} else if ( ! port2->is_input() && port1->is_input()) {
		src_port = port2;
		dst_port = port1;
	} else {
		return;
	}
	
	if (are_connected(src_port, dst_port))
		disconnect(src_port, dst_port);
	else
		connect(src_port, dst_port);
}


/** Event handler for ports.
 *
 * These events can't be handled in the Port class because they have to do with
 * connections etc. which deal with multiple ports (ie _selected_port).  Ports
 * pass their events on to this function to get around this.
 */
bool
FlowCanvas::port_event(GdkEvent* event, boost::weak_ptr<Port> weak_port)
{
	boost::shared_ptr<Port> port = weak_port.lock();
	if (!port)
		return false;

	static bool port_dragging = false;
	bool handled = true;
	
	switch (event->type) {
	
	case GDK_BUTTON_PRESS:
		if (event->button.button == 1) {
			port_dragging = true;
		} else if (event->button.button == 3) {
			_selected_port = port;
			port->popup_menu(event->button.button, event->button.time);
		} else {
			handled = false;
		}
		break;

	case GDK_BUTTON_RELEASE:
		if (port_dragging) {
			if (_connect_port == NULL) {
				selected_port(port);
				_connect_port = port;
			} else {
				ports_joined(port, _connect_port);
				_connect_port.reset();
				selected_port(boost::shared_ptr<Port>());
			}
			port_dragging = false;
		} else {
			handled = false;
		}
		_base_rect.ungrab(event->button.time);
		break;

	case GDK_ENTER_NOTIFY:
		if (port != _selected_port)
			port->set_highlighted(true);
		break;

	case GDK_LEAVE_NOTIFY:
		if (port_dragging) {
			_drag_state = CONNECTION;
			_connect_port = port;
			
			_base_rect.grab(GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
				Gdk::Cursor(Gdk::CROSSHAIR), event->button.time);

			port_dragging = false;
		} else {
			if (port != _selected_port)
				port->set_highlighted(false);
		}
		break;

	default:
		handled = false;
	}
	
	return handled;
}


bool
FlowCanvas::canvas_event(GdkEvent* event)
{
	// Hack around bug I can't nail down :)
	if (event->type == GDK_BUTTON_RELEASE) {
		_base_rect.ungrab(event->button.time);
	}

	return false;
}


/* I can not get this to work for the life of me.
 * Man I hate gnomecanvas.
bool
FlowCanvas::scroll_drag_handler(GdkEvent* event)
{
	
	bool handled = true;
	
	static int    original_scroll_x = 0;
	static int    original_scroll_y = 0;
	static double origin_x = 0;
	static double origin_y = 0;
	static double x_offset = 0;
	static double y_offset = 0;
	static double scroll_offset_x = 0;
	static double scroll_offset_y = 0;
	static double last_x = 0;
	static double last_y = 0;

	bool first_motion = true;
	
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 2) {
		_base_rect.grab(GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
			Gdk::Cursor(Gdk::FLEUR), event->button.time);
		get_scroll_offsets(original_scroll_x, original_scroll_y);
		scroll_offset_x = original_scroll_x;
		scroll_offset_y = original_scroll_y;
		origin_x = event->button.x;
		origin_y = event->button.y;
		last_x = origin_x;
		last_y = origin_y;
		first_motion = true;
		_scroll_dragging = true;
		
	} else if (event->type == GDK_MOTION_NOTIFY && _scroll_dragging) {
		// These are world-relative coordinates
		double x = event->motion.x_root;
		double y = event->motion.y_root;
		
		//c2w(x, y, x, y);
		//world_to_window(x, y, x, y);
		//window_to_world(event->button.x, event->button.y, x, y);
		//w2c(x, y, x, y);

		x_offset += last_x - x;//x + original_scroll_x;
		y_offset += last_y - y;// + original_scroll_y;
		
		//cerr << "Coord: (" << x << "," << y << ")\n";
		//cerr << "Offset: (" << x_offset << "," << y_offset << ")\n";

		int temp_x;
		int temp_y;
		w2c(lrint(x_offset), lrint(y_offset),
			temp_x, temp_y);
		scroll_offset_x += temp_x;
		scroll_offset_y += temp_y;
		scroll_to(scroll_offset_x,
		          scroll_offset_y);
		last_x = x;
		last_y = y;
	} else if (event->type == GDK_BUTTON_RELEASE && _scroll_dragging) {
		_base_rect.ungrab(event->button.time);
		_scroll_dragging = false;
	} else {
		handled = false;
	}

	return handled;
	return false;
}
*/


bool
FlowCanvas::select_drag_handler(GdkEvent* event)
{
	boost::shared_ptr<Module> module;
	
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 1) {
		assert(_select_rect == NULL);
		_drag_state = SELECT;
		if ( !(event->button.state & GDK_CONTROL_MASK))
			clear_selection();
		_select_rect = new Gnome::Canvas::Rect(*root(),
			event->button.x, event->button.y, event->button.x, event->button.y);
		_select_rect->property_fill_color_rgba() = 0x273344FF;
		_select_rect->property_outline_color_rgba() = 0xEEEEFFFF;
		_select_rect->property_width_units() = 0.5;
		_select_rect->lower_to_bottom();
		_base_rect.lower_to_bottom();
		return true;
	} else if (event->type == GDK_MOTION_NOTIFY && _drag_state == SELECT) {
		assert(_select_rect);
		double x = event->button.x, y = event->button.y;
		
		if (event->motion.is_hint) {
			gint t_x;
			gint t_y;
			GdkModifierType state;
			gdk_window_get_pointer(event->motion.window, &t_x, &t_y, &state);
			x = t_x;
			y = t_y;
		}
		_select_rect->property_x2() = x;
		_select_rect->property_y2() = y;
		return true;
	} else if (event->type == GDK_BUTTON_RELEASE && _drag_state == SELECT) {
		// Select all modules within rect
		for (ModuleMap::iterator i = _modules.begin(); i != _modules.end(); ++i) {
			module = (*i).second;
			if (module->is_within(*_select_rect)) {
				if (module->selected())
					unselect_module(module);
				else
					select_module(module);
			}
		}
		
		_base_rect.ungrab(event->button.time);
		
		delete _select_rect;
		_select_rect = NULL;
		_drag_state = NOT_DRAGGING;
		return true;
	}
	return false;
}


/** Updates _select_dash for rotation effect, and updates any
  * selected item's borders (and the selection rectangle).
  */
bool
FlowCanvas::animate_selected()
{
	static int i = 0;
	
	i = (i+1) % 10;
	
	_select_dash->offset = i;
	
	for (list<boost::shared_ptr<Module> >::iterator m = _selected_modules.begin(); m != _selected_modules.end(); ++m)
		(*m)->_module_box.property_dash() = _select_dash;
	
	for (list<boost::shared_ptr<Connection> >::iterator c = _selected_connections.begin(); c != _selected_connections.end(); ++c)
		(*c)->property_dash() = _select_dash;
	
	return true;
}


bool
FlowCanvas::connection_drag_handler(GdkEvent* event)
{
	bool handled = true;
	
	// These are invisible, just used for making connections (while dragging)
	static boost::shared_ptr<Module> drag_module;
	static boost::shared_ptr<Port>   drag_port;
	
	static boost::shared_ptr<Connection> drag_connection;
	static boost::shared_ptr<Port>       snapped_port;

	static bool snapped = false;
	
	/*if (event->type == GDK_BUTTON_PRESS && event->button.button == 2) {
		_drag_state = SCROLL;
	} else */if (event->type == GDK_MOTION_NOTIFY && _drag_state == CONNECTION) {
		double x = event->button.x, y = event->button.y;
		
		if (event->motion.is_hint) {
			gint t_x;
			gint t_y;
			GdkModifierType state;
			gdk_window_get_pointer(event->motion.window, &t_x, &t_y, &state);
			x = t_x;
			y = t_y;
		}
		root()->w2i(x, y);

		if (!drag_connection) { // Havn't created the connection yet
			assert(drag_port == NULL);
			assert(_connect_port);
			
			drag_module = boost::shared_ptr<Module>(new Module(shared_from_this(), "", x, y));
			bool drag_port_is_input = true;
			if (_connect_port->is_input())
				drag_port_is_input = false;
				
			drag_port = boost::shared_ptr<Port>(new Port(drag_module, "", drag_port_is_input, _connect_port->color()));

			//drag_port->hide();
			drag_module->hide();

			drag_module->move_to(x, y);
			
			drag_port->property_x() = 0;
			drag_port->property_y() = 0;
			drag_port->_rect.property_x2() = 1;
			drag_port->_rect.property_y2() = 1;
			if (drag_port_is_input)
				drag_connection = boost::shared_ptr<Connection>(new Connection(shared_from_this(), _connect_port, drag_port));
			else
				drag_connection = boost::shared_ptr<Connection>(new Connection(shared_from_this(), drag_port, _connect_port));
				
			drag_connection->update_location();
		}

		if (snapped) {
			if (drag_connection)
				drag_connection->hide();

			boost::shared_ptr<Port> p = get_port_at(x, y);

			if (drag_connection)
				drag_connection->show();

			if (p) {
				boost::shared_ptr<Module> m = p->module().lock();
				if (m) {
					if (p != _selected_port) {
						if (snapped_port)
							snapped_port->set_highlighted(false);
						p->set_highlighted(true);
						snapped_port = p;
					}
					drag_module->property_x() = m->property_x().get_value();
					drag_module->_module_box.property_x2() = m->_module_box.property_x2().get_value();
					drag_module->property_y() = m->property_y().get_value();
					drag_module->_module_box.property_y2() = m->_module_box.property_y2().get_value();
					drag_port->property_x() = p->property_x().get_value();
					drag_port->property_y() = p->property_y().get_value();
				}
			} else {  // off the port now, unsnap
				if (snapped_port)
					snapped_port->set_highlighted(false);
				snapped_port.reset();
				snapped = false;
				drag_module->property_x() = x;
				drag_module->property_y() = y;
				drag_port->property_x() = 0;
				drag_port->property_y() = 0;
				drag_port->_rect.property_x2() = 1;
				drag_port->_rect.property_y2() = 1;
			}
			drag_connection->update_location();
		} else { // not snapped to a port
			assert(drag_module);
			assert(drag_port);
			assert(_connect_port);

			// "Snap" to port, if we're on a port and it's the right direction
			if (drag_connection)
				drag_connection->hide();
			
			boost::shared_ptr<Port> p = get_port_at(x, y);
			
			if (drag_connection)
				drag_connection->show();
			
			if (p && p->is_input() != _connect_port->is_input()) {
				boost::shared_ptr<Module> m = p->module().lock();
				if (m) {
					p->set_highlighted(true);
					snapped_port = p;
					snapped = true;
					// Make drag module and port exactly the same size/loc as the snapped
					drag_module->move_to(m->property_x().get_value(), m->property_y().get_value());
					drag_module->set_width(m->width());
					drag_module->set_height(m->height());
					drag_port->property_x() = p->property_x().get_value();
					drag_port->property_y() = p->property_y().get_value();
					// Make the drag port as wide as the snapped port so the connection coords are the same
					drag_port->_rect.property_x2() = p->_rect.property_x2().get_value();
					drag_port->_rect.property_y2() = p->_rect.property_y2().get_value();
				}
			} else {
				drag_module->property_x() = x;
				drag_module->property_y() = y - 7; // FIXME: s#7#cursor_height/2#
			}
			drag_connection->update_location();
		}
	} else if (event->type == GDK_BUTTON_RELEASE && _drag_state == CONNECTION) {
		_base_rect.ungrab(event->button.time);
		
		double x = event->button.x;
		double y = event->button.y;
		_base_rect.i2w(x, y);

		if (drag_connection)
			drag_connection->hide();
		
		boost::shared_ptr<Port> p = get_port_at(x, y);
		
		if (drag_connection)
			drag_connection->show();
	
		if (p) {
			if (p == _connect_port) {   // drag ended on same port it started on
				if (_selected_port == NULL) {  // no active port, just activate (hilite) it
					selected_port(_connect_port);
				} else {  // there is already an active port, connect it with this one
					if (_selected_port != _connect_port)
						ports_joined(_selected_port, _connect_port);
					unselect_ports();
					_connect_port.reset();
					snapped_port.reset();
				}
			} else {  // drag ended on different port
				//p->set_highlighted(false);
				ports_joined(_connect_port, p);
				unselect_ports();
				_connect_port.reset();
				snapped_port.reset();
			}
		}
		
		// Clean up dragging stuff
		
		_base_rect.ungrab(event->button.time);
		
		if (_connect_port)
			_connect_port->set_highlighted(false);

		_drag_state = NOT_DRAGGING;
		drag_connection.reset();
		drag_module.reset();
		drag_port.reset();
		unselect_ports();
		snapped_port.reset();
		_connect_port.reset();
	} else {
		handled = false;
	}

	return handled;
}


boost::shared_ptr<Port>
FlowCanvas::get_port_at(double x, double y)
{
	// Loop through every port and see if the item at these coordinates is that port
	// (if you're thinking this is slow, stupid, and disgusting, you're right)
	for (ModuleMap::const_iterator i = _modules.begin(); i != _modules.end(); ++i) {
		const boost::shared_ptr<Module> m = (*i).second;
		
		if (m->point_is_within(x, y))
			return m->port_at(x, y);

	}
	return boost::shared_ptr<Port>();
}


} // namespace LibFlowCanvas
