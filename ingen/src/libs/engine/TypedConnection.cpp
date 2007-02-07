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

#include "TypedConnection.h"
#include "util.h"
#include "InputPort.h"
#include "OutputPort.h"
#include "Node.h"
#include "Port.h"

namespace Ingen {


/** Constructor for a connection from a node's output port.
 *
 * This handles both polyphonic and monophonic nodes, transparently to the 
 * user (InputPort).
 */
template <typename T>
TypedConnection<T>::TypedConnection(OutputPort<T>* const src_port, InputPort<T>* const dst_port)
: Connection(src_port, dst_port),
  _local_buffer(NULL),
  _must_mix(true),
  _buffer_size(src_port->buffer_size()),
  _pending_disconnection(false)
{
	assert((src_port->parent_node()->poly() == dst_port->parent_node()->poly())
		|| (src_port->parent_node()->poly() == 1 || dst_port->parent_node()->poly() == 1));

	if (src_port->poly() == dst_port->poly())
		_must_mix = false;
	else // Poly -> Mono connection, need a buffer to mix into
		_local_buffer = new Buffer<T>(_buffer_size);

	//cerr << "Connection " << src_port->path() << " -> " << dst_port->path()
	//	<< "\t mixing: " << _must_mix << endl;
}
template TypedConnection<Sample>::TypedConnection(OutputPort<Sample>* const src_port, InputPort<Sample>* const dst_port);
template TypedConnection<MidiMessage>::TypedConnection(OutputPort<MidiMessage>* const src_port, InputPort<MidiMessage>* const dst_port);


template <typename T>
TypedConnection<T>::~TypedConnection()
{
	delete _local_buffer;
}
template TypedConnection<Sample>::~TypedConnection();
template TypedConnection<MidiMessage>::~TypedConnection();


/** Allocate a mixdown buffer if necessary */
template <typename T>
void
TypedConnection<T>::set_buffer_size(size_t size)
{
	if (_must_mix) {
		assert(_local_buffer);
		delete _local_buffer;

		_local_buffer = new Buffer<T>(size);
	}
	
	_buffer_size = size;
}


template <typename Sample>
void
TypedConnection<Sample>::process(SampleCount nframes, FrameTime start, FrameTime end)
{
	// FIXME: nframes parameter not used
	assert(_buffer_size == 1 || _buffer_size == nframes);

	/* Thought:  A poly output port can be connected to multiple mono input
	 * ports, which means this mix down would have to happen many times.
	 * Adding a method to OutputPort that mixes down all it's outputs into
	 * a buffer (if it hasn't been done already this cycle) and returns that
	 * would avoid having to mix multiple times.  Probably not a very common
	 * case, but it would be faster anyway. */
	
	if (_must_mix) {
		//cerr << "Mixing " << src_port()->buffer(0)->data()
		//	<< " -> " << _local_buffer->data() << endl;

		_local_buffer->copy(src_port()->buffer(0), 0, _buffer_size-1);
	
		// Mix all the source's voices down into local buffer starting at the second
		// voice (buffer is already set to first voice above)
		for (size_t j=1; j < src_port()->poly(); ++j)
			_local_buffer->accumulate(src_port()->buffer(j), 0, _buffer_size-1);

		// Scale the buffer down.
		if (src_port()->poly() > 1)
			_local_buffer->scale(1.0f/(float)src_port()->poly(), 0, _buffer_size-1);
	}
}
template void TypedConnection<Sample>::process(SampleCount nframes, FrameTime start, FrameTime end);


// FIXME: MIDI mixing not implemented
template <>
void
TypedConnection<MidiMessage>::process(SampleCount nframes, FrameTime start, FrameTime end)
{
}


} // namespace Ingen

