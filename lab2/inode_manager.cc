#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
    bzero(blocks, sizeof(blocks));
}

void disk::read_block(blockid_t id, char *buf)
{
    /*
     *your lab1 code goes here.
     *if id is smaller than 0 or larger than BLOCK_NUM
     *or buf is null, just return.
     *put the content of target block into buf.
     *hint: use memcpy
     */
    if (id < 0 || id > BLOCK_NUM || buf == NULL) {
        return;
    }

    memcpy(buf, this->blocks[id], BLOCK_SIZE);
}

void disk::write_block(blockid_t id, const char *buf)
{
    /*
     *your lab1 code goes here.
     *hint: just like read_block
     */
    if (id < 0 || id > BLOCK_NUM || buf == NULL) {
        return;
    }

    memcpy(this->blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

int block_manager::alloc_block_by_id(uint32_t id) {
    char buf[BLOCK_SIZE];
    this->read_block(BBLOCK(id), buf);

    char ch = buf[id % BLOCK_SIZE];
    std::bitset<8> bit_set(ch);
    if (bit_set.test(id % 8)) {
        fprintf(stderr, "block id = %u is used\n", id);
        return -1;
    }
    bit_set.set(id % 8);
    buf[id % BLOCK_SIZE] = (char) bit_set.to_ulong();
    this->write_block(BBLOCK(id), buf);

    return 0;
}

// Allocate a free disk block.
blockid_t block_manager::alloc_block()
{
    /*
     * your lab1 code goes here.
     * note: you should mark the corresponding bit in block bitmap when alloc.
     * you need to think about which block you can start to be allocated.

     *hint: use macro IBLOCK and BBLOCK.
     use bit operation.
     remind yourself of the layout of disk.
     */
    uint32_t original_next = this->next_alloc;
    while (true) {
        if (this->is_free(this->next_alloc)) {
            this->alloc_block_by_id(this->next_alloc);
            this->next_alloc ++;
            return this->next_alloc - 1;
        } else {
            this->next_alloc ++;
            if (this->next_alloc == BLOCK_NUM + this->first_block) {
                this->next_alloc = this->first_block;
            }
            if (this->next_alloc == original_next) {
                fprintf(stderr, "!!!!!!no more space to alloc\n");
                return 0;
            }
        }
    }

}

bool block_manager::is_free(uint32_t pos) {
    char buf[BLOCK_SIZE];
    this->read_block(BBLOCK(pos), buf);
    char ch = buf[pos % BLOCK_SIZE];
    std::bitset<8> bit_set(ch);
    return !bit_set.test(pos % 8);
}

void block_manager::free_block(uint32_t id)
{
    /*
     * your lab1 code goes here.
     * note: you should unmark the corresponding bit in the block bitmap when free.
     */
    char buf[BLOCK_SIZE];
    this->read_block(BBLOCK(id), buf);

    size_t B_pos = id % BLOCK_SIZE;
    size_t b_pos = id % 8;

    std::bitset<8> bit_set(buf[B_pos]);
    bit_set.reset(b_pos);
    buf[B_pos] = (char)bit_set.to_ulong();
    this->write_block(BBLOCK(id), buf);
    this->next_alloc = id;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
    d = new disk();

    // format the disk
    sb.size = DISK_SIZE;
    sb.nblocks = BLOCK_NUM;
    sb.ninodes = INODE_NUM;
    first_block = sb.nblocks / (BPB) + INODE_NUM + 3;

    this->next_alloc = first_block;
}

void block_manager::read_block(uint32_t id, char *buf)
{
    d->read_block(id, buf);
}

void block_manager::write_block(uint32_t id, const char *buf)
{
    d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
    this->next_inode_num = 1;
    bm = new block_manager();
    uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
    if (root_dir != 1) {
        printf("\tim: error! alloc first inode %u, should be 1\n", root_dir);
        exit(0);
    }
}

blockid_t inode_manager::get_inode_block_id(uint32_t inum, int pos) {
    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL) {
        return -1;
    }

    int off = pos / BLOCK_SIZE;
    return i_node->blocks[off];
}

/* Create a new file.
 * Return its inum. */
uint32_t inode_manager::alloc_inode(uint32_t type)
{
    /*
     * your lab1 code goes here.
     * note: the normal inode block should begin from the 2nd inode block.
     * the 1st is used for root_dir, see inode_manager::inode_manager().

     * if you get some heap memory, do not forget to free it.
     */
    inode_t *i_node;

    uint32_t original_inode = this->next_inode_num;
    while (this->get_inode(this->next_inode_num) != NULL) {
        if (this->next_inode_num == INODE_NUM) {
            this->next_inode_num = 1;
        }
        this->next_inode_num ++;
        if (this->next_inode_num == original_inode) {
            return 0;
        }
    }
    i_node = (inode_t *)malloc(sizeof(inode_t));
    bzero(i_node, sizeof(inode_t));

    i_node->type = type;
    i_node->size = 0;
    i_node->atime = time(NULL);
    i_node->ctime = time(NULL);
    i_node->mtime = time(NULL);

    this->put_inode(this->next_inode_num, i_node);
    free(i_node);
    this->next_inode_num ++;
    return this->next_inode_num - 1;
}

void inode_manager::free_inode(uint32_t inum)
{
    /*
     * your lab1 code goes here.
     * note: you need to check if the inode is already a freed one;
     * if not, clear it, and remember to write back to disk.
     * do not forget to free memory if necessary.
     */

    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL) {
        return;
    }

    uint32_t i;
    for (i = 0; i <= NDIRECT; i++) {
        if (i_node->blocks[i] != 0) {
            this->bm->free_block(i_node->blocks[i]);
        }
    }
    bzero(i_node, sizeof(inode_t));
    this->put_inode(inum, i_node);
    this->next_inode_num = inum;
}

void inode_manager::free_indirect_block(blockid_t id) {
    uint32_t indirect_buf[NINDIRECT];
    this->bm->read_block(id, (char *) indirect_buf);
    for (uint32_t id = 0; id < NINDIRECT; id ++) {
        if (indirect_buf[id] != 0) {
            this->bm->free_block(indirect_buf[id]);
        }
    }
}

void inode_manager::echo_inode(inode_t *i_node) {
    printf("\tdumping inode info\n");
    printf("\t\ttype: %u, size: %u\n", i_node->type, i_node->size);
    printf("\t\taccess time: %lu, ctime: %lu, mtime: %lu\n", i_node->atime, i_node->ctime, i_node->mtime);
    printf("\t\tblocks: ");
    for (uint32_t ii = 0; ii <= NDIRECT; ii++) {
        printf(" %u ", i_node->blocks[ii]);
    }
    printf("\n");
}

void inode_manager::echo_inode_by_id(uint32_t id) {
    this->echo_inode(this->get_inode(id));
}

/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* inode_manager::get_inode(uint32_t inum)
{
    struct inode *ino, *ino_disk;
    char buf[BLOCK_SIZE];

    printf("\tim: get_inode %d\n", inum);

    if (inum < 0 || inum >= INODE_NUM) {
        printf("\tim: inum out of range\n");
        return NULL;
    }

    bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
    // printf("%s:%d\n", __FILE__, __LINE__);

    ino_disk = (struct inode*)buf + inum % IPB;
    if (ino_disk->type == 0) {
        printf("\tim: inode not exist\n");
        return NULL;
    }

    ino = (struct inode*)malloc(sizeof(struct inode));
    *ino = *ino_disk;

    return ino;
}

void inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
    char buf[BLOCK_SIZE];
    struct inode *ino_disk;

    printf("\tim: put_inode %d\n", inum);
    if (ino == NULL)
        return;

    bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
    ino_disk = (struct inode*)buf + inum % IPB;
    *ino_disk = *ino;
    bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

void inode_manager::read_block(uint32_t i_num, int pos, char *buf) {
    inode_t *i_node = this->get_inode(i_num);
    if (i_node == NULL || pos < 0 || pos > NDIRECT) {
        fprintf(stderr, "invalid block");
        return;
    }

    this->bm->read_block(i_node->blocks[pos], buf);
}

/* Get all the data of a file by inum.
 * Return alloced data, should be freed by caller. */
void inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
    /*
     * your lab1 code goes here.
     * note: read blocks related to inode number inum,
     * and copy them to buf_out
     */
    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL || *size < 0) {
        return;
    }

    uint32_t node_size = i_node->size;
    *buf_out = (char *)malloc(node_size);
    bzero(*buf_out, node_size);
    char *buf_ptr = *buf_out;

    int i;
    char tmp[BLOCK_SIZE];
    for (i = 0; i <= NDIRECT ; ++i) {
        if (i < NDIRECT) {
            /* handle direct inode */
            bzero(tmp, BLOCK_SIZE);
            this->bm->read_block(i_node->blocks[i], tmp);
            size_t block_size = MIN(BLOCK_SIZE, node_size - (*size));

            memcpy(buf_ptr, tmp, block_size);
            buf_ptr += block_size;
            (*size) += block_size;

            if ((uint32_t)(*size) >= node_size) {
                break;
            }
        } else {
            /* handle indirect inode */
            this->read_indirect_block(i_node->blocks[i], &buf_ptr, size, node_size);
        }

    }

    /* update inode */
    i_node->atime = time(NULL);
    this->put_inode(inum, i_node);
}

void inode_manager::read_indirect_block(blockid_t id, char **buf_out, int *size, uint32_t total_size) {
    char *buf_ptr = *buf_out;

    char *block_buf = (char *)malloc(BLOCK_SIZE * sizeof(char));
    this->bm->read_block(id, block_buf);
    uint32_t *indirect_buf = (uint32_t *) block_buf;

    uint32_t i ;
    char tmp[BLOCK_SIZE];
    for (i = 0; i < NINDIRECT; ++i ) {
        if (indirect_buf[i] != 0) {
            this->bm->read_block(indirect_buf[i], tmp);
            size_t read_size = MIN(BLOCK_SIZE, total_size - *size);

            memcpy(buf_ptr, tmp, read_size);
            buf_ptr += read_size;
            (*size) += read_size;
        } else {
            break;
        }

        if ((uint32_t)(*size) >= total_size) {
            break;
        }
    }

    free(block_buf);
}

/* alloc/free blocks if needed */
void inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
    /*
     * your lab1 code goes here.
     * note: write buf to blocks of inode inum.
     * you need to consider the situation when the size of buf
     * is larger or smaller than the size of original inode.
     * you should free some blocks if necessary.
     */
    std::cout << "===== >> buf going to write for " << inum << ":'" << buf << "'" << std::endl;
    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL || size < 0) {
        return;
    }

    char *buf_ptr = (char *) buf;

    unsigned int written_size = 0;
    unsigned int original_size = i_node->size;
    unsigned int i;
    char tmp[BLOCK_SIZE];
    for (i = 0; i <= NDIRECT; ++i) {
        if (i < NDIRECT) {
            /* write to direct block */
            if (written_size >= original_size || i_node->blocks[i] == 0) {
                /* alloc new block if buf is larger than old one */
                blockid_t alloc_id = this->bm->alloc_block();
                i_node->blocks[i] = alloc_id;
            }
            size_t written_block_size = MIN(BLOCK_SIZE, size - written_size);
            bzero(tmp, BLOCK_SIZE);
            memcpy(tmp, buf_ptr, written_block_size);
            this->bm->write_block(i_node->blocks[i], tmp);

            written_size += written_block_size;
            buf_ptr += written_block_size;

            if (written_size >= (unsigned int) size) {
                if (original_size > (unsigned int)size) {
                    /* free block if necessary */
                    unsigned int j;
                    for (j = i + 1; j <= NDIRECT; ++j) {
                        if (i_node->blocks[j] != 0) {
                            if (j == NDIRECT) {
                                this->free_indirect_block(i_node->blocks[NDIRECT]);
                            }
                            this->bm->free_block(i_node->blocks[j]);
                            i_node->blocks[j] = 0;
                        }
                    }
                }

                break;
            }
        } else {
            /* write to indirect block if necessary */
            if (written_size >= original_size) {
                i_node->blocks[i] = this->bm->alloc_block();
            }
            this->write_indirect_block(i_node->blocks[i], buf_ptr,
                                       &written_size, (unsigned int) size, original_size);
        }
    }

    /* update inode */
    i_node->size = (unsigned int)size;
    i_node->ctime = time(NULL);
    i_node->mtime = time(NULL);
    this->put_inode(inum, i_node);
}

void inode_manager::write_indirect_block(blockid_t id, char *buf,
                                         unsigned int *written_size,
                                         unsigned int real_size,
                                         unsigned int original_size) {
    char *buf_ptr = buf;
    uint32_t indirect_buf[NINDIRECT];
    this->bm->read_block(id, (char *)indirect_buf);

    unsigned int allocated_indirect_block;
    if (original_size > *written_size) {
        allocated_indirect_block = (original_size - *written_size) / BLOCK_SIZE;
    } else {
        allocated_indirect_block = 0;
    }

    unsigned int i = 0;
    char tmp[BLOCK_SIZE];
    while ((*written_size) < real_size && i < NINDIRECT) {
        if (i > allocated_indirect_block || indirect_buf[i] == 0) {
            blockid_t alloc_id = this->bm->alloc_block();
            indirect_buf[i] = alloc_id;
        }

        /* write real data */
        size_t block_written_size = MIN(BLOCK_SIZE, real_size - *written_size);
        bzero(tmp, BLOCK_SIZE);
        memcpy(tmp, buf_ptr, block_written_size);
        this->bm->write_block(indirect_buf[i], tmp);

        (*written_size) += block_written_size;
        buf_ptr += block_written_size;

        i++;
    }
    /* free block if necessary */
    unsigned int j;
    for (j = i; j < NINDIRECT; ++j) {
        if (indirect_buf[j] != 0) {
            this->bm->free_block(indirect_buf[j]);
        }
        indirect_buf[j] = 0;
    }

    this->bm->write_block(id, (char *) indirect_buf);
}

void inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
    /*
     * your lab1 code goes here.
     * note: get the attributes of inode inum.
     * you can refer to "struct attr" in extent_protocol.h
     */
    inode_t *node = this->get_inode(inum);
    if (node == NULL) {
        return;
    }

    a.size = node->size;
    a.type = node->type;
    a.ctime = node->ctime;
    a.mtime = node->mtime;
    a.atime = node->atime;
}

int inode_manager::setattr(uint32_t inum, extent_protocol::attr &a) {
    inode_t *node = this->get_inode(inum);
    if (node == NULL) {
        return -1;
    }

    node->size = a.size;
    node->type = a.type;
    node->atime = a.atime;
    node->ctime = a.ctime;
    node->mtime = a.mtime;
    this->put_inode(inum, node);

    return 0;
}

void inode_manager::remove_file(uint32_t inum)
{
    /*
     * your lab1 code goes here
     * note: you need to consider about both the data block and inode of the file
     * do not forget to free memory if necessary.
     */
    this->free_inode(inum);
}
