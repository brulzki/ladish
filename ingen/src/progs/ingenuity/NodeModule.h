/* This file is part of Ingen.  Copyright (C) 2006 Dave Robillard.
 * 
 * Ingen is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 * 
 * Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef NODEMODULE_H
#define NODEMODULE_H

#include <string>
#include <boost/enable_shared_from_this.hpp>
#include <libgnomecanvasmm.h>
#include <flowcanvas/Module.h>
#include "raul/SharedPtr.h"
#include "Port.h"
#include "NodeMenu.h"
using std::string;

class Atom;

namespace Ingen { namespace Client {
	class PortModel;
	class NodeModel;
	class ControlModel;
} }
using namespace Ingen::Client;

namespace Ingenuity {
	
class PatchCanvas;
class Port;


/** A module in a patch.
 *
 * This base class is extended for various types of modules - SubpatchModule,
 * DSSIModule, etc.
 *
 * \ingroup Ingenuity
 */
class NodeModule : public boost::enable_shared_from_this<NodeModule>, public LibFlowCanvas::Module
{
public:
	static boost::shared_ptr<NodeModule> create (boost::shared_ptr<PatchCanvas> canvas, SharedPtr<NodeModel> node);

	virtual ~NodeModule();

	boost::shared_ptr<Port> port(const string& port_name) {
		return boost::dynamic_pointer_cast<Ingenuity::Port>(
			Module::get_port(port_name));
	}

	virtual void store_location();

	void on_right_click(GdkEventButton* event);
	
	void show_control_window();

	SharedPtr<NodeModel> node() const { return _node; }

protected:
	NodeModule(boost::shared_ptr<PatchCanvas> canvas, SharedPtr<NodeModel> node);

	virtual void on_double_click(GdkEventButton* ev) { show_control_window(); }
	virtual void on_middle_click(GdkEventButton* ev) { show_control_window(); }
	
	void metadata_update(const string& key, const Atom& value);
	
	void add_port(SharedPtr<PortModel> port, bool resize=true);
	void remove_port(SharedPtr<PortModel> port);
	
	SharedPtr<NodeModel> _node;
	NodeMenu             _menu;
};


} // namespace Ingenuity

#endif // NODEMODULE_H
