#ifndef yfs_client_h
#define yfs_client_h

#include <string>
#include <cstring>
#include "extent_client.h"
#include <vector>
#include <list>
#include <sstream>
#include <fstream>


class yfs_client {
    extent_client *ec;
public:

    typedef unsigned long long inum;
    enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
    typedef int status;

    struct fileinfo {
        unsigned long long size;
        unsigned long atime;
        unsigned long mtime;
        unsigned long ctime;
    };
    struct dirinfo {
        unsigned long atime;
        unsigned long mtime;
        unsigned long ctime;
    };
    struct symlink_info {
        unsigned int size;
        unsigned long atime;
        unsigned long mtime;
        unsigned long ctime;
    } symlink_info_t;
    struct dirent {
        std::string name;
        yfs_client::inum inum;
    };
    typedef struct dirent dirent_t;

private:
    static std::string filename(inum);
    static inum n2i(std::string);
    int add_file_to_dir(uint32_t dir_inode_num, uint32_t file_inode_num, const char *filename);
    int remove_file_from_dir(uint32_t dir, const char *name);
    int remove_file_from_dir(uint32_t dir, inum ino);

public:
    yfs_client();
    yfs_client(std::string, std::string);

    bool isfile(inum);
    bool isdir(inum);
    extent_protocol::type_t get_file_type(inum i_node_num);

    int getfile(inum, fileinfo &);
    int getdir(inum, dirinfo &);
    int get_link(inum, symlink_info &);

    int setattr(inum, uint32_t);
    int lookup(inum, const char *, bool &, inum &);
    int create(inum, const char *, mode_t, inum &);

    int create_symlink(inum parent, const char *src, const char *dest, inum &ino_out);
    int read_symlink(inum link_number, std::string & buf_out);

    int readdir(inum, std::list<dirent> &);
    int write_dir(inum, const std::list<dirent>);
    int write(inum, size_t, off_t, const char *, size_t &);
    int read(inum, size_t, off_t, std::string &);
    int unlink(inum,const char *);
    int mkdir(inum , const char *, mode_t , inum &);

    /** you may need to add symbolic link related methods here.*/
};

#endif 
