#include "inode_manager.h"
#include <pthread.h>
#include <cassert>
// disk layer -----------------------------------------

char *rs_encode_block(char *buffer);
char *rs_decode_block(char *encoded_buffer);

disk::disk()
{
  pthread_t id;
  int ret;
  bzero(blocks, sizeof(blocks));

  ret = pthread_create(&id, NULL, test_daemon, (void*)blocks);
  if(ret != 0)
	  printf("FILE %s line %d:Create pthread error\n", __FILE__, __LINE__);
}

void disk::read_block(blockid_t id, char *buf)
{
    if (id < 0 || id > BLOCK_NUM || buf == NULL) {
        return;
    }

    memcpy(buf, this->blocks[id], BLOCK_SIZE);
}

void disk::write_block(blockid_t id, const char *buf)
{
    if (id < 0 || id > BLOCK_NUM || buf == NULL) {
        return;
    }

    memcpy(this->blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t block_manager::alloc_block()
{
    uint32_t original_next = this->next_alloc;
    while (true) {
        if (this->is_free(this->next_alloc) && next_alloc >= first_block) {
            if (alloc_block_by_id(this->next_alloc) == 0) {
                printf("alloc_block: alloced %d\n", next_alloc);
                this->next_alloc ++;
                return this->next_alloc - 1;
            } else {
                next_alloc ++;
                continue;
            }
        } else {
            this->next_alloc ++;
            if (this->next_alloc == BLOCK_NUM) {
                this->next_alloc = this->first_block;
            }
            if (this->next_alloc == original_next) {
                fprintf(stderr, "!!!!!!no more space to alloc\n");
                return 0;
            }
        }
    }

}

int block_manager::alloc_block_by_id(uint32_t id) {
    char buf[REAL_SIZE];
    this->read_block(BBLOCK(id), buf);

    char ch = buf[id % REAL_SIZE];
    std::bitset<8> bit_set(ch);
    if (bit_set.test(id % 8)) {
        fprintf(stderr, "block id = %u is used\n", id);
        return -1;
    }
    bit_set.set(id % 8, true);
    buf[id % REAL_SIZE] = (char) bit_set.to_ulong();
    this->write_block(BBLOCK(id), buf);

    return 0;
}

bool block_manager::is_free(uint32_t id) {
    char buf[REAL_SIZE];
    this->read_block(BBLOCK(id), buf);
    char ch = buf[id % REAL_SIZE];
    std::bitset<8> bit_set(ch);
    return bit_set.test(id % 8) == false;
}

void block_manager::free_block(uint32_t id)
{
    char buf[REAL_SIZE];
    this->read_block(BBLOCK(id), buf);

    size_t B_pos = id % REAL_SIZE;
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
    first_block = sb.nblocks / (BPB) + sb.ninodes * (BPI) + 3;

    this->next_alloc = first_block;
}

void block_manager::read_block(uint32_t id, char *buf)
{
    char tmp[BLOCK_SIZE];
    d->read_block(id, tmp);
    char *decoded_buf = rs_decode_block(tmp);
    memcpy(buf, decoded_buf, REAL_SIZE);
}

void block_manager::write_block(uint32_t id, char *buf)
{
    char *encoded_buf = rs_encode_block(buf);
    d->write_block(id, encoded_buf);
}

void block_manager::read_block_direct(uint32_t id, char *buf) {
    d->read_block(id, buf);
}

void block_manager::write_block_direct(uint32_t id, char *buf) {
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
    uint32_t original_inode = this->next_inode_num;
    while (this->get_inode(this->next_inode_num)) {
        if (this->next_inode_num == INODE_NUM) {
            this->next_inode_num = 1;
        }
        this->next_inode_num ++;
        if (this->next_inode_num == original_inode) {
            printf("alloc_inode: no more to alloc\n");
            return INODE_NUM + 1;
        }
    }
    inode_t *i_node = (inode_t *)malloc(sizeof(*i_node));
    bzero(i_node, sizeof(*i_node));

    i_node->type = type;
    i_node->size = 0;
    i_node->atime = (unsigned int) time(NULL);
    i_node->ctime = (unsigned int) time(NULL);
    i_node->mtime = (unsigned int) time(NULL);

    this->put_inode(this->next_inode_num, i_node);
    free(i_node);
    printf("alloc_inode: alloced %d\n", this->next_inode_num);
    this->next_inode_num ++;
    return this->next_inode_num - 1;
}

void inode_manager::free_inode(uint32_t inum)
{
    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL) {
        printf("free_inode: nothing to do, inode not exist\n");
        return;
    }

    size_t i;
    for (i = 0; i < NDIRECT + NINDIRECT_META; i++) {
        if (i_node->blocks[i] != 0) {
            if (i >= NDIRECT) {
                this->free_indirect_block(i_node->blocks[i]);
            }
            this->bm->free_block(i_node->blocks[i]);
        }
    }
    bzero(i_node, sizeof(inode_t));
    this->put_inode(inum, i_node);
    this->next_inode_num = inum;
}

void inode_manager::free_indirect_block(blockid_t id) {
    uint32_t indirect_buf[REAL_NINDIRECT];
    this->bm->read_block(id, (char *) indirect_buf);
    for (size_t i = 0; i < REAL_NINDIRECT; i ++) {
        if (indirect_buf[i] != 0) {
            this->bm->free_block(indirect_buf[i]);
            indirect_buf[i] = 0;
        }
    }
    this->bm->write_block(id, (char *) indirect_buf);
}

void inode_manager::echo_inode(inode_t *i_node) {
    printf("\tdumping inode info\n");
    printf("\t\ttype: %u, size: %llu\n", i_node->type, i_node->size);
    printf("\t\taccess time: %u, ctime: %u, mtime: %u\n", i_node->atime, i_node->ctime, i_node->mtime);
    printf("\t\tblocks: ");
    for (uint32_t ii = 0; ii < NDIRECT + NINDIRECT_META; ii++) {
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
    uint32_t block_id1 = IBLOCK(inum, this->bm->sb.nblocks);
    char *buf = (char *) malloc(REAL_SIZE * BPI);

    printf("\tim: get_inode %d\n", inum);

    if (inum <= 0 || inum > INODE_NUM) {
        printf("\tim: inum out of range\n");
        return NULL;
    }

    bm->read_block(block_id1, buf);
    // bm->read_block(block_id1 + 1, buf + REAL_SIZE);
    struct inode *ino = (struct inode *) buf;

    // echo_inode(ino);

    if (ino->type == 0) {
        printf("\tim: inode %u not exist\n", inum);
        return NULL;
    }

    return ino;
}

void inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
    printf("\tim: put_inode %d\n", inum);
    if (ino == NULL) {
        printf("do not put a inode with NULL\n");
        return;
    }

    echo_inode(ino);
    char *ptr = (char *) ino;
    uint32_t block_id1 = IBLOCK(inum, bm->sb.nblocks);
    bm->write_block(block_id1, ptr);
    // bm->write_block(block_id1 + 1, ptr + REAL_SIZE);
    printf("put_inode: done\n");
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
    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL || *size < 0) {
        printf("read_file: nothing read\n");
        return;
    }

    unsigned long long node_size = i_node->size;
    *buf_out = (char *)malloc(node_size);
    bzero(*buf_out, node_size);
    char *buf_ptr = *buf_out;

    int i;
    char tmp[REAL_SIZE];
    for (i = 0; i < NDIRECT + NINDIRECT_META ; ++i) {
        if (i < NDIRECT) {
            /* handle direct inode */
            bzero(tmp, REAL_SIZE);
            this->bm->read_block(i_node->blocks[i], tmp);
            size_t read_size = MIN(REAL_SIZE, node_size - (*size));

            memcpy(buf_ptr, tmp, read_size);
            buf_ptr += read_size;
            (*size) += read_size;

        } else {
            /* handle indirect inode */
            int old_size = *size;
            this->read_indirect_block(i_node->blocks[i], &buf_ptr, size, node_size);
            buf_ptr += (*size - old_size);
        }

        if ((uint32_t)(*size) >= node_size) {
            break;
        }

    }

    /* update inode */
    i_node->atime = (unsigned int) time(NULL);
    this->put_inode(inum, i_node);
}

void inode_manager::read_indirect_block(blockid_t id, char **buf_out, int *size, uint32_t total_size) {
    char *buf_ptr = *buf_out;

    uint32_t indirect_buf[REAL_NINDIRECT];
    this->bm->read_block(id, (char*) indirect_buf);

    uint32_t i ;
    char tmp[REAL_SIZE];
    for (i = 0; i < REAL_NINDIRECT; ++i ) {
        if (indirect_buf[i] != 0) {
            this->bm->read_block(indirect_buf[i], tmp);
            size_t read_size = MIN(REAL_SIZE, total_size - *size);

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

}

void inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
    std::cout << "write_file for " << inum << ":'" << buf << "'" << "size: " << size << std::endl;
    inode_t *i_node = this->get_inode(inum);
    if (i_node == NULL || size < 0) {
        printf("inode not exist while writing file %d\n", inum);
        return;
    }

    char *buf_ptr = (char *) buf;

    unsigned long long written_size = 0;
    unsigned long long original_size = i_node->size;
    unsigned int i;
    char original_tmp[REAL_SIZE];
    for (i = 0; i < NDIRECT + NINDIRECT_META; ++i) {
        if (i < NDIRECT) {
            /* write to direct block */
            if (written_size >= original_size || i_node->blocks[i] == 0) {
                /* alloc new block if buf is larger than old one */
                blockid_t alloc_id = this->bm->alloc_block();
                i_node->blocks[i] = alloc_id;
            }
            /* written_block_size: size write to a block that is real data */
            size_t written_block_size = MIN(REAL_SIZE, size - written_size);
            bzero(original_tmp, REAL_SIZE);
            memcpy(original_tmp, buf_ptr, written_block_size);
            this->bm->write_block(i_node->blocks[i], original_tmp);

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
            if (written_size >= size) {
                for (int j = i; j < NDIRECT + NINDIRECT_META; j++) {
                    this->free_indirect_block(i_node->blocks[j]);
                    this->bm->free_block(i_node->blocks[j]);
                    i_node->blocks[j] = 0;
                }
                break;
            }
            if (written_size < size && i_node->blocks[i] == 0) {
                i_node->blocks[i] = this->bm->alloc_block();
            }
            int old_size = written_size;
            this->write_indirect_block(i_node->blocks[i], buf_ptr,
                                       &written_size, (unsigned int) size, original_size);
            buf_ptr += (written_size - old_size);
        }
    }

    /* update inode */
    i_node->size = (unsigned long long) size;
    i_node->atime = (unsigned int) time(NULL);
    i_node->ctime = (unsigned int) time(NULL);
    i_node->mtime = (unsigned int) time(NULL);
    this->put_inode(inum, i_node);
}

void inode_manager::write_indirect_block(blockid_t id, char *buf,
                                         unsigned long long *written_size,
                                         unsigned int real_size,
                                         unsigned long long original_size) {
    char *buf_ptr = buf;
    uint32_t indirect_buf[REAL_NINDIRECT]; // size = REAL_NINDIRECT
    this->bm->read_block(id, (char *) indirect_buf);

    unsigned int i = 0;
    char original_tmp[REAL_SIZE];
    while (i < REAL_NINDIRECT) {
        if (*written_size >= real_size) {
            break;
        }
        if (indirect_buf[i] == 0) {
            indirect_buf[i] = this->bm->alloc_block();
        }

        /* write real data */
        size_t block_written_size = MIN(REAL_SIZE, real_size - *written_size);
        bzero(original_tmp, REAL_SIZE);
        memcpy(original_tmp, buf_ptr, block_written_size);
        this->bm->write_block(indirect_buf[i], original_tmp);

        (*written_size) += block_written_size;
        buf_ptr += block_written_size;

        i++;
    }

    /* free block if necessary */
    unsigned int j;
    for (j = i; j < REAL_NINDIRECT; ++j) {
        if (indirect_buf[j] != 0) {
            this->bm->free_block(indirect_buf[j]);
            indirect_buf[j] = 0;
        }
    }

    this->bm->write_block(id, (char *) indirect_buf);
}

void inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
    inode_t *node = this->get_inode(inum);
    if (node == NULL) {
        a.type = extent_protocol::T_NOTEXIST;
        return;
    }

    a.size = node->size;
    a.type = node->type;
    a.ctime = node->ctime;
    a.mtime = node->mtime;
    a.atime = node->atime;
    a.uid = node->uid;
    a.gid = node->gid;
    a.mode = node->mode;
}

int inode_manager::setattr(uint32_t inum, extent_protocol::attr &a) {
    inode_t *node = this->get_inode(inum);
    if (node == NULL) {
        return -1;
    }

    // node->size = a.size;
    if (a.mode) {
        node->mode = a.mode;
    }
    if (a.uid) {
        node->uid = a.uid;
    }
    if (a.gid) {
        node->gid = a.gid;
    }

    /* atime, ctime, mtime, type can't not modified directly
     * it will modified automatically by the inode manager
     * size will auto changed after write new content to the file
     */
    node->ctime = (unsigned int) time(NULL);
    this->put_inode(inum, node);

    return 0;
}

void inode_manager::remove_file(uint32_t inum)
{
    this->free_inode(inum);
}


static char rs_encode_4bit(std::bitset<4> sets);
static std::bitset<4> rs_decode_byte(char chr, bool &double_err);
static bool hamming_check_c1(std::bitset<8> encoded_sets);
static bool hamming_check_c2(std::bitset<8> encoded_sets);
static bool hamming_check_c3(std::bitset<8> encoded_sets);
static bool hamming_check_c4(std::bitset<8> encoded_sets);

static char *rs_encode_byte(char chr);
static char rs_decode_4bytes(char *encoded_bytes);
static char rs_byte_flip(char buf, unsigned int pos);

/*
 * to encode a BLOCK SIZE buffer
 */
char *rs_encode_block(char *buffer) {
    // std::cout << "buffer to encode: " << buffer << std::endl;
    char *buf = (char *) malloc(REAL_SIZE * sizeof(char));
    bzero(buf, REAL_SIZE);
    memcpy(buf, buffer, REAL_SIZE);
    // printf("rs_encode_block: buf: %s\n", buf);

    char *encoded_buffer = (char *) malloc(BLOCK_SIZE * sizeof(char));
    bzero(encoded_buffer, BLOCK_SIZE * sizeof(char));
    char *buf_ptr = encoded_buffer;
    size_t index = 0;
    while (index < REAL_SIZE) {
        char *encode_bytes = rs_encode_byte(buf[index]);
        memcpy(buf_ptr, encode_bytes, 4);
        buf_ptr += 4;
        index ++;
    }

    free(buf);

    return encoded_buffer;
}

char *rs_decode_block(char *encoded_buffer) {
    char *buffer = (char*) malloc(BLOCK_SIZE);
    bzero(buffer, BLOCK_SIZE);
    memcpy(buffer, encoded_buffer, BLOCK_SIZE);

    char *decoded_buffer = (char *) malloc(REAL_SIZE * sizeof(char));
    bzero(decoded_buffer, REAL_SIZE * sizeof(char));
    char *buf_ptr = decoded_buffer;

    size_t index = 0;
    char *ptr = buffer;
    while (index < BLOCK_SIZE) {
        *buf_ptr = rs_decode_4bytes(ptr);
        ptr += 4;
        buf_ptr ++;

        index += 4;
    }
    free(buffer);
    return decoded_buffer;
}

static void echo_4byte(char *bytes) {
    for (int i = 0; i< 4; i++) {
        std::cout << std::hex << "0x"  << int(bytes[i]) << " ";
    }
}
/*
 * encode a byte size data
 * 1 byte => 4 bytes
 * bang
 */
static char *rs_encode_byte(char chr) {
    char *encoded_buffer = (char *) malloc(4 * sizeof(char));
    bzero(encoded_buffer, 4 * sizeof(char));
    char encoded_chr1 = rs_encode_4bit(
            std::bitset<4>((unsigned long) ((chr >> 4) & 0x0f)));
    char encoded_chr2 = rs_encode_4bit(
            std::bitset<4>((unsigned long) (chr & 0x0f)));
    encoded_buffer[0] = encoded_buffer[1] = char (encoded_chr1 & 0xff);
    encoded_buffer[2] = encoded_buffer[3] = char (encoded_chr2 & 0xff);
    /*
    std::cout << "encode byte from " << int(chr) << " TO " ;
    echo_4byte(encoded_buffer);
    std::cout << std::endl;
     */
    return encoded_buffer;
}

static char rs_decode_4bytes(char *encoded_bytes) {
    bool double_err = false;
    std::bitset<4> decoded_sets1, decoded_sets2;
    decoded_sets1 = rs_decode_byte(encoded_bytes[0], double_err);
    if (double_err) {
        decoded_sets1 = rs_decode_byte(encoded_bytes[1], double_err);
    }

    decoded_sets2 = rs_decode_byte(encoded_bytes[2], double_err);
    if (double_err) {
        decoded_sets2 = rs_decode_byte(encoded_bytes[3], double_err);
    }

    char c1 = (char)decoded_sets1.to_ulong();
    char c2 = (char)decoded_sets2.to_ulong();
    /*
    std::cout << "decode 4byte from " ;
    echo_4byte(encoded_bytes);
    std::cout << " to " << std::hex <<"Ox" << ((c1 << 4) | c2) << std::endl;
     */
    return (c1 << 4) | c2;
}

static char rs_encode_4bit(std::bitset<4> sets) {
    std::bitset<8> encoded_bits;
    // copy origin text
    for (size_t i = 0; i < 4; i++) {
        encoded_bits.set(i, sets.test(i));
    }

    bool c;
    c = sets.test(0) ^ sets.test(1) ^ sets.test(2);
    encoded_bits.set(4, c);
    c = sets.test(0) ^ sets.test(1) ^ sets.test(3);
    encoded_bits.set(5, c);
    c = sets.test(0) ^ sets.test(2) ^ sets.test(3);
    encoded_bits.set(6, c);

    c = encoded_bits.test(0);
    for (size_t i = 1; i < 7; i ++) {
        c ^= encoded_bits.test(i);
    }
    encoded_bits.set(7, c);

    // std::cout <<"encode 4bit from " << sets.to_string() << " to " << encoded_bits.to_string() << std::endl;
    return (char) (encoded_bits.to_ulong());
}

static std::bitset<4> rs_decode_byte(char chr, bool &double_err) {
    double_err = false;
    std::bitset<8> encoded_sets((unsigned long)chr);
    bool c1_correct = hamming_check_c1(encoded_sets);
    bool c2_correct = hamming_check_c2(encoded_sets);
    bool c3_correct = hamming_check_c3(encoded_sets);
    bool c4_correct = hamming_check_c4(encoded_sets);

    if (!c4_correct) {
        /* single bit error */
        if (!c1_correct && !c2_correct && !c3_correct) {
            encoded_sets.flip(0);
        } else if (!c1_correct && !c2_correct) {
            encoded_sets.flip(1);
        } else if (!c1_correct && !c3_correct) {
            encoded_sets.flip(2);
        } else if (!c2_correct && !c3_correct) {
            encoded_sets.flip(3);
        } else if (!c1_correct) {
            encoded_sets.flip(4);
        } else if (!c2_correct) {
            encoded_sets.flip(5);
        } else if (!c3_correct) {
            encoded_sets.flip(6);
        } else {
            encoded_sets.flip(7);
        }
    } else {
        /* double bits error */
        double_err = true;
        /*
        if (!c1_correct && !c2_correct) {
            encoded_sets.flip(4);
            encoded_sets.flip(5);
        } else if (!c1_correct && !c3_correct) {
            encoded_sets.flip(4);
            encoded_sets.flip(6);
        } else if (!c2_correct && !c3_correct) {
            encoded_sets.flip(5);
            encoded_sets.flip(6);
        }
         */
    }

    std::bitset<4> decoded_sets;
    for (size_t i = 0; i < 4; i++) {
        decoded_sets.set(i, encoded_sets.test(i));
    }
    // std::cout << "decode byte from " << encoded_sets.to_string() << " to " << decoded_sets.to_string() << std::endl;
    return decoded_sets;
}

static bool hamming_check_c1(std::bitset<8> encoded_sets) {
    return (encoded_sets.test(0) ^ encoded_sets.test(1) ^ encoded_sets.test(2))
           == encoded_sets.test(4);
}
static bool hamming_check_c2(std::bitset<8> encoded_sets) {
    return (encoded_sets.test(0) ^ encoded_sets.test(1) ^ encoded_sets.test(3))
           == encoded_sets.test(5);
}
static bool hamming_check_c3(std::bitset<8> encoded_sets) {
    return (encoded_sets.test(0) ^ encoded_sets.test(2) ^ encoded_sets.test(3))
           == encoded_sets.test(6);
}
static bool hamming_check_c4(std::bitset<8> encoded_sets) {
    bool c = encoded_sets.test(0);
    for (size_t i = 1; i < 7; i++) {
        c ^= encoded_sets.test(i);
    }
    return c == encoded_sets.test(7);
}

static char rs_byte_flip(char buf, unsigned int pos) {
    assert(pos < 7 && pos >= 0);
    return (char) (buf ^ (0x1 << (7 - pos)));
}
