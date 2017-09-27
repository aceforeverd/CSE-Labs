// inode layer interface.

#ifndef inode_h
#define inode_h

#include "extent_protocol.h"
#include <bitset>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <stdint.h>
#include <time.h>

#define DISK_SIZE  (1024*1024*16)
#define BLOCK_SIZE 512
#define BLOCK_NUM  (DISK_SIZE/BLOCK_SIZE)

typedef uint32_t blockid_t;

// disk layer -----------------------------------------

class disk {
    private:
        unsigned char blocks[BLOCK_NUM][BLOCK_SIZE];

    public:
        disk();
        void read_block(uint32_t id, char *buf);
        void write_block(uint32_t id, const char *buf);
};

// block layer -----------------------------------------

typedef struct superblock {
    uint32_t size;
    uint32_t nblocks;
    uint32_t ninodes;
} superblock_t;

class block_manager {
    private:
        disk *d;
        std::map<uint32_t, int> using_blocks;

        int first_free_bit_of_char(char ch);
        int first_free_char_of_buf(char *buf);
        int mask_bit(char *ch, int pos);
        int unmask_bit(char *ch, int pos);
        bool is_free(uint32_t pos);
        int alloc_block_by_id(uint32_t id);
        blockid_t next_alloc;
    public:
        block_manager();
        struct superblock sb;

        uint32_t alloc_block();
        void free_block(uint32_t id);
        void read_block(uint32_t id, char *buf);
        void write_block(uint32_t id, const char *buf);
};

// inode layer -----------------------------------------

#define INODE_NUM  1024

// Inodes per block.
#define IPB    1
//(BLOCK_SIZE / sizeof(struct inode))

// Block containing inode i
#define IBLOCK(i, nblocks)     ((nblocks)/BPB + (i)/IPB + 3)

// Bitmap bits per block
#define BPB           (BLOCK_SIZE * 8)

// Block containing bit for block b
#define BBLOCK(b) ((b) / BPB + 2)

#define NDIRECT 32
#define NINDIRECT (BLOCK_SIZE / sizeof(unsigned int))
#define MAXFILE (NDIRECT + NINDIRECT)

typedef struct inode {
    //short type;
    unsigned int type;
    unsigned int size;
    unsigned int atime;
    unsigned int mtime;
    unsigned int ctime;
    blockid_t blocks[NDIRECT+1];   // Data block addresses
} inode_t;

class inode_manager {
    private:
        block_manager *bm;
        struct inode* get_inode(uint32_t inum);
        void put_inode(uint32_t inum, struct inode *ino);
        void read_indirect_block(blockid_t id, char **buf_out, int *size, uint32_t total_size);
        void write_indirect_block(blockid_t id, char *buf, unsigned int *written_size, unsigned int real_size, unsigned int original_size);
        void free_indirect_block(blockid_t id);

        uint32_t next_inode_num;

    public:
        inode_manager();
        uint32_t alloc_inode(uint32_t type);
        void free_inode(uint32_t inum);
        void read_file(uint32_t inum, char **buf, int *size);
        void read_block(uint32_t i_num, int pos, char *buf);
        void write_file(uint32_t inum, const char *buf, int size);
        void remove_file(uint32_t inum);
        void getattr(uint32_t inum, extent_protocol::attr &a);
        void echo_inode(inode_t *);
        void echo_inode_by_id(uint32_t id);
        blockid_t get_inode_block_id(uint32_t inum, int pos);
};

#endif

