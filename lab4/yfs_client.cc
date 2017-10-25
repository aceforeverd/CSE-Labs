// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void echo_dir_list(std::list<yfs_client::dirent> list) {
    /* debug */
    std::cout << "echo dir info ..." << std::endl;
    for (std::list<yfs_client::dirent>::iterator it = list.begin(); it != list.end(); it++) {
        std::cout << it->inum << " '" << it->name << "'" << std::endl;
    }
    std::cout << "echo done ." << std::endl;

}


yfs_client::yfs_client(std::string extent_dst) {
    ec = new extent_client(extent_dst);
    if (ec->put(1, "") != extent_protocol::OK)
        printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst) {
    ec = new extent_client(extent_dst);
    lc = new lock_client(lock_dst);
    if (ec->put(1, "") != extent_protocol::OK)
        printf("error init root dir\n"); // XYB: init root dir
}

/* string => unsigned long long */
yfs_client::inum yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    yfs_client::inum finum;
    ist >> finum;
    return finum;
}

/* unsigned long long => string */
std::string yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool yfs_client::isfile(inum i_node_num)
{
    extent_protocol::attr a;

    if (ec->getattr(i_node_num, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", i_node_num);
        return true;
    }
    printf("isfile: %lld is a dir\n", i_node_num);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool yfs_client::isdir(inum i_node_num)
{
    extent_protocol::attr i_node_attr;

    if (ec->getattr(i_node_num, i_node_attr) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    return i_node_attr.type == extent_protocol::T_DIR;
}

extent_protocol::type_t yfs_client::get_file_type(inum i_node_num) {
    extent_protocol::attr i_node_attr;

    if (ec->getattr(i_node_num, i_node_attr) != extent_protocol::OK) {
        printf("error getting attr\n");
        return extent_protocol::T_UNKNOWN;
    }

    return static_cast<extent_protocol::type_t>(i_node_attr.type);
}

int yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr i_node_attr;
    if (ec->getattr(inum, i_node_attr) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = i_node_attr.atime;
    fin.mtime = i_node_attr.mtime;
    fin.ctime = i_node_attr.ctime;
    fin.size = i_node_attr.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

    release:
    return r;
}

int yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

    release:
    return r;
}

int yfs_client::get_link(inum ino, symlink_info & info) {
    int r = OK;

    printf("get link info %016llx\n", ino);
    extent_protocol::attr a;
    if (ec->getattr(ino, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    info.size = a.size;
    info.atime = a.atime;
    info.ctime = a.ctime;
    info.mtime = a.mtime;

    release:
    return r ;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int yfs_client::setattr(inum ino, uint32_t size)
{
    /*
     * your lab2 code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
    lc->acquire(ino);

    int r = OK;
    std::string buf;
    fileinfo info;
    if (getfile(ino, info) != OK) {
        std::cerr << "failed to get file info" << std::endl;
        r = IOERR;
        goto release;
    }

    if (read(ino, info.size, 0, buf) != OK) {
        std::cerr << "failed to read file buf" << std::endl;
        r = IOERR;
        goto release;
    }

    if (info.size > size) {
        buf = buf.substr(0, size) ;
    } else if (info.size < size) {
        buf.append(std::string(size - info.size, '\0'));
    }

    if (ec->put(ino, buf) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    release:
    lc->release(ino);
    return r;
}

int yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    lc->acquire(parent);
    /* check parent is diectory */
    int r = OK;
    bool found = false;
    inum ino;

    if (!isdir(parent)) {
        std::cerr << "parent is not a directory" << std::endl;
        r = NOENT;
        goto release;
    }

    this->lookup(parent, name, found, ino);
    if (found) {
        std::cerr << "failed to create: file with same name exist" << std::endl;
        r = EXIST;
        goto release;
    }

    if (ec->create(extent_protocol::T_FILE, ino_out) != extent_protocol::OK) {
        std::cerr << "failed to create new file" << std::endl;
        r = IOERR;
        goto release;
    }

    this->add_file_to_dir((uint32_t)parent, (uint32_t)ino_out, name);

    release:
    lc->release(parent);
    return r;
}

int yfs_client::create_symlink(inum parent, const char *src, const char *dest, inum &ino_out) {
    lc->acquire(parent);

    int r = OK;

    bool found = false;
    inum ino;
    std::string buf(src);

    lookup(parent, dest, found, ino);
    if (found) {
        r = EXIST;
        goto release;
    }

    if (ec->create(extent_protocol::T_SYMLINK, ino_out) != extent_protocol::OK) {
        std::cerr << "failed to create new symlink file" << std::endl;
        r = IOERR;
        goto release;
    }

    /* add content to linker file */
    if (ec->put(ino_out, buf) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    this->add_file_to_dir(parent, ino_out, dest);

    release:
    lc->release(parent);
    return r;
}

int yfs_client::read_symlink(inum link_number, std::string &buf_out) {
    if (ec->get(link_number, buf_out) != extent_protocol::OK) {
        return IOERR;
    }
    return OK;
}

int yfs_client::add_file_to_dir(uint32_t dir_inode_num,
                                uint32_t file_inode_num,
                                const char *filename) {
    bool found = false;
    inum ino_out;
    this->lookup(dir_inode_num, filename, found, ino_out);
    if (found) {
        return EXIST;
    }

    std::list<dirent> dir_list;
    this->readdir(dir_inode_num, dir_list);
    dirent_t dir_obj;
    dir_obj.inum = file_inode_num;
    dir_obj.name = std::string(filename);
    dir_list.push_back(dir_obj);

    this->write_dir(dir_inode_num, dir_list);
    return OK;
}

int yfs_client::remove_file_from_dir(uint32_t dir, const char *name) {
    std::list<dirent> list;
    this->readdir(dir, list);

    std::list<dirent_t>::iterator is;
    for (std::list<dirent_t>::iterator it = list.begin(); it != list.end(); it ++) {
        if (it->name == name) {
            is = it;
        }
    }
    list.erase(is);
    this->write_dir(dir, list);
    return OK;
}

int yfs_client::remove_file_from_dir(uint32_t dir, inum ino) {
    std::cout << "!!!! removing file from dir" << std::endl;
    std::list<dirent> list;
    this->readdir(dir, list);

    std::list<dirent_t>::iterator is;
    for (std::list<dirent>::iterator it = list.begin(); it != list.end(); it++ ) {
        if (it->inum == ino) {
            is = it;
            break;
        }
    }
    list.erase(is);
    this->write_dir(dir, list);
    std::cout << "!!! done" << std::endl;
    return OK;
}

int yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    lc->acquire(parent);

    int r = OK;
    bool found = false;
    inum ino;

    if (!isdir(parent)) {
        std::cerr << "parent is not a directory" << std::endl;
        r = NOENT;
        goto release;
    }

    this->lookup(parent, name, found, ino);
    if (found) {
        std::cerr << "failed to mkdir: directory with same name exist" << std::endl;
        r = EXIST;
        goto release;
    }

    if (ec->create(extent_protocol::T_DIR, ino_out) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    this->add_file_to_dir(parent, ino_out, name);

    release:
    lc->release(parent);
    return r;
}

int yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    /*
     * your lab2 code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
    if (!isdir(parent)) {
        std::cerr << "not a directory" << std::endl;
        return NOENT;
    }

    std::string s_name(name);

    found = false;
    std::list<dirent> dir_list;
    this->readdir(parent, dir_list);
    for (std::list<dirent>::iterator dir_obj = dir_list.begin(); dir_obj != dir_list.end(); dir_obj ++) {
        if (dir_obj->name.compare(s_name) == 0) {
            found = true;
            ino_out = dir_obj->inum;
            return EXIST;
        }
    }
    return NOENT;
}

int yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    /*
     * your lab2 code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
    std::string buf;
    ec->get(dir, buf);

    std::stringstream ss(buf);
    dirent_t dir_obj;

    list.clear();
    size_t file_length;
    while (ss >> dir_obj.inum >> file_length) {
        /* skip one space */
        ss.get();

        char tmp[file_length + 1];
        ss.read(tmp, file_length);
        tmp[file_length] = '\0';
        dir_obj.name = std::string(tmp);
        if (dir_obj.inum > 0) {
            list.push_back(dir_obj);
        }

        /* skip end character '\n' */
        ss.get();
    }

    return OK;
}

int yfs_client::write_dir(inum ino, std::list<dirent> list) {
    std::cout << "list to write into dir" << std::endl;
    echo_dir_list(list);

    std::stringstream ss(" ");
    for (std::list<dirent>::iterator dir_obj = list.begin(); dir_obj != list.end(); dir_obj ++) {
        ss << dir_obj->inum << " " << dir_obj->name.length()
                                  << " " << dir_obj->name << "\n";
        std::cout << "buf_tmp & buf: " << ss.str() << std::endl;
    }

    if (ec->put(ino, ss.str()) != extent_protocol::OK) {
        std::cerr << "failed to update dir list" << std::endl;
        return IOERR;
    }

    return OK;
}

int yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    /*
     * your lab2 code goes here.
     * note: read using ec->get().
     */
    std::string buf;
    if (ec->get(ino, buf) != extent_protocol::OK) {
        return IOERR;
    }

    data = buf.substr(off, size);
    return OK;
}

int yfs_client::write(inum ino, size_t size, off_t off, const char *data,
                  size_t &bytes_written)
{
    /*
     * your lab2 code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
    lc->acquire(ino);

    int r = OK;
    size_t length;
    std::string buf;

    if (off < 0) {
        std::cerr << "position unreachable" << std::endl;
        r = IOERR;
        goto release;
    }

    if (ec->get(ino, buf) != extent_protocol::OK) {
        std::cerr << "error while reading inode data" << std::endl;
        r = IOERR;
        goto release;
    }
    length = buf.size();

    if (off + size > length) {
        buf.resize(off + size, '\0');
    }
    buf.replace(off, size, std::string(data, size));

    if (ec->put(ino, buf) != extent_protocol::OK) {
        std::cerr << "error while writing the data" << std::endl;
        r = IOERR;
        goto release;
    }
    bytes_written = size;

    release:
    lc->release(ino);
    return r;
}

int yfs_client::unlink(inum parent, const char *name)
{
    /*
     * your lab2 code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
    lc->acquire(parent);

    int r = OK;

    inum file;
    bool found;
    this->lookup(parent, name, found, file);
    if (!found) {
        std::cerr << "file name = " << name << "not found" << std::endl;
        r = NOENT;
        goto release;
    }

    if (isdir(file)) {
        /* currently not support remove dir */
        r = EXIST;
        goto release;
    }

    if (ec->remove(file) != extent_protocol::OK) {
        std::cerr << "error happen while unlinking the file" << std::endl;
        r = IOERR;
        goto release;
    }
    this->remove_file_from_dir(parent, file);

    release:
    lc->release(parent);
    return r;
}

