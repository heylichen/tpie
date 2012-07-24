// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; eval: (progn (c-set-style "stroustrup") (c-set-offset 'innamespace 0)); -*-
// vi:set ts=4 sts=4 sw=4 noet :
// Copyright 2009, 2010, The TPIE development team
//
// This file is part of TPIE.
//
// TPIE is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// TPIE is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with TPIE.  If not, see <http://www.gnu.org/licenses/>
#include <tpie/portability.h>
#include <tpie/exception.h>
#include <tpie/file_base.h>
#include <tpie/memory.h>
#include <stdlib.h>

namespace {
static tpie::stream_size_type the_block_size=0;
}

namespace tpie {

memory_size_type get_block_size() {
	if (the_block_size == 0) {
		const char * v = getenv("TPIE_BLOCK_SIZE");
		if (v != NULL) the_block_size = atol(v);
		if (the_block_size == 0) the_block_size=1024*1024*2; //Default block size is 2MB
	}
	return the_block_size;
}

void set_block_size(memory_size_type block_size) {
	the_block_size=block_size;
}

/*************************> file_base_crtp <*******************************/ 
template <typename child_t>
file_base_crtp<child_t>::file_base_crtp(
	memory_size_type itemSize, double blockFactor,
	file_accessor::file_accessor * fileAccessor) {
	m_size = 0;
	m_itemSize = itemSize;
	m_open = false;
	if (fileAccessor == 0)
		fileAccessor = new default_file_accessor();
	m_fileAccessor = fileAccessor;

	m_blockSize = block_size(blockFactor);
	m_blockItems = m_blockSize/m_itemSize;
	m_tempFile = 0;
}

template <typename child_t>
template <typename BT>
void file_base_crtp<child_t>::read_block(BT & b, stream_size_type block) {
	b.dirty = false;
	b.number = block;
	
	// calculate buffer size
	b.size = m_blockItems;
	if (static_cast<stream_size_type>(b.size) + b.number * static_cast<stream_size_type>(m_blockItems) > self().size())
		b.size = static_cast<memory_size_type>(self().size() - block * m_blockItems);
	
	// populate buffer data
	if (b.size > 0 &&
		m_fileAccessor->read_block(b.data, b.number, b.size) != b.size) {
		throw io_exception("Incorrect number of items read");
	}
}

template <typename child_t>
void file_base_crtp<child_t>::get_block_check(stream_size_type block) {
	// If the file contains n full blocks (numbered 0 through n-1), we may
	// request any block in {0, 1, ... n}

	// If the file contains n-1 full blocks and a single non-full block, we may
	// request any block in {0, 1, ... n-1}

	// We capture this restraint with the assertion:
	if (block * static_cast<stream_size_type>(m_blockItems) > self().size()) {
		throw end_of_stream_exception();
	}
}

/*********************************> stream_crtp <*******************************/
template <typename child_t>
void stream_crtp<child_t>::update_block() {
	if (m_nextBlock == std::numeric_limits<stream_size_type>::max()) {
		m_nextBlock = self().__block().number+1;
		m_nextIndex = 0;
	}
	self().update_block_core();
	m_blockStartIndex = m_nextBlock*static_cast<stream_size_type>(self().__file().block_items());
	m_index = m_nextIndex;
	m_nextBlock = std::numeric_limits<stream_size_type>::max();
	m_nextIndex = std::numeric_limits<memory_size_type>::max();
}


/*******************************> file_stream_base <******************************/
file_stream_base::file_stream_base(memory_size_type itemSize,
								   double blockFactor,
								   file_accessor::file_accessor * fileAccessor):
	file_base_crtp<file_stream_base>(itemSize, blockFactor, fileAccessor) 
{
	m_blockStartIndex = 0;
	m_nextBlock = std::numeric_limits<stream_size_type>::max();
	m_nextIndex = std::numeric_limits<memory_size_type>::max();
	m_index = std::numeric_limits<memory_size_type>::max();
	m_block.data = 0;
}

void file_stream_base::get_block(stream_size_type block) {
	get_block_check(block);
	read_block(m_block, block);
}

void file_stream_base::update_block_core() {
	flush_block();
	get_block(m_nextBlock);
}

/***********************************> file_base <*********************************/
file_base::file_base(memory_size_type itemSize,
					 double blockFactor,
					 file_accessor::file_accessor * fileAccessor):
	file_base_crtp<file_base>(itemSize, blockFactor, fileAccessor) {
	m_emptyBlock.size = 0;
	m_emptyBlock.number = std::numeric_limits<stream_size_type>::max();
}


void file_base::create_block() {
	// alloc heap block
	block_t * block = reinterpret_cast<block_t*>( tpie_new_array<char>(sizeof(block_t) + m_itemSize*m_blockItems) );

	// call ctor
	new (block) block_t();

	// push to intrusive list
	m_free.push_front(*block);
}

void file_base::delete_block() {
	// find first block
	assert(!m_free.empty());
	block_t * block = &m_free.front();

	// remove from intrusive list
	m_free.pop_front();

	// call dtor
	block->~block_t();

	// dealloc
	tpie_delete_array<char>(reinterpret_cast<char*>(block), sizeof(block_t) + m_itemSize*m_blockItems);
}


file_base::block_t * file_base::get_block(stream_size_type block) {
	block_t * b;
	get_block_check(block);

	// First, see if the block is already buffered
	boost::intrusive::list<block_t>::iterator i = m_used.begin();
	while (i != m_used.end() && i->number != block)
		++i;

	if (i == m_used.end()) {
		// block not buffered. populate a free buffer.
		assert(!m_free.empty());

		// fetch a free buffer
		b = &m_free.front();
		b->usage = 0;
		read_block(*b, block);

		b->dirty = false;
		b->number = block;

		// read went well. move buffer to m_used
		m_free.pop_front();
		m_used.push_front(*b);

	} else {
		// yes, the block is already buffered.
		b = &*i;
	}
	++b->usage;
	return b;
}

void file_base::free_block(block_t * block) {
	assert(block->usage > 0);
	--block->usage;
	if (block->usage > 0) return;

	if (block->dirty || !m_canRead) {
		assert(m_canWrite);
		m_fileAccessor->write_block(block->data, block->number, block->size);
	}

	boost::intrusive::list<block_t>::iterator i = m_used.iterator_to(*block);

	m_used.erase(i);

	m_free.push_front(*block);
}

void file_base::close() {
	assert(m_free.empty());
	assert(m_used.empty());
	p_t::close();
}

file_base::~file_base() {
	assert(m_free.empty());
	assert(m_used.empty());
	delete m_fileAccessor;
}

/*************************> file_base::stream <*******************************/
void file_base::stream::update_block_core() {
	if (m_block != &m_file.m_emptyBlock) {
		m_file.free_block(m_block);
		m_block = &m_file.m_emptyBlock; // necessary if get_block below throws
	}
	m_block = m_file.get_block(m_nextBlock);
}


file_base::stream::stream(file_base & f, stream_size_type offset):
	m_file(f) {
	m_blockStartIndex = 0;
	m_nextBlock = std::numeric_limits<stream_size_type>::max();
	m_nextIndex = std::numeric_limits<memory_size_type>::max();
	m_index = std::numeric_limits<memory_size_type>::max();;
	m_block = &m_file.m_emptyBlock;
	m_file.create_block();
	if (m_file.m_open)
		seek(offset);
}

void file_base::stream::free() {
	if (m_block) {
		if (m_block != &m_file.m_emptyBlock) m_file.free_block(m_block);
		m_file.delete_block();
	}
	m_block = 0;
}

file_base::block_t file_base::m_emptyBlock;


template class stream_crtp<file_stream_base>;
template class stream_crtp<file_base::stream>;
template class file_base_crtp<file_stream_base>;
template class file_base_crtp<file_base>;

} //namespace tpie

