// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

extent_client::extent_client()
{
    es = new extent_server();
}

extent_server *extent_client::get_extent_server() {
    return this->es;
}

blockid_t extent_client::get_inode_block_id(uint32_t inum, int pos) {
    return this->get_extent_server()->get_inode_manager()->get_inode_block_id(inum, pos);
}

int extent_client::read_block(uint32_t i_num, int pos, char *buf) {
    this->es->get_inode_manager()->read_block(i_num, pos, buf);
    return 0;
}

extent_protocol::status
extent_client::create(uint32_t type, extent_protocol::extentid_t &id)
{
    extent_protocol::status ret = extent_protocol::OK;
    ret = es->create(type, id);
    return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    ret = es->get(eid, buf);
    return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid,
                       extent_protocol::attr &attr)
{
    extent_protocol::status ret = extent_protocol::OK;
    ret = es->getattr(eid, attr);
    return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    int r;
    ret = es->put(eid, buf, r);
    return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
    extent_protocol::status ret = extent_protocol::OK;
    int r;
    ret = es->remove(eid, r);
    return ret;
}


