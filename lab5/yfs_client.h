#ifndef yfs_client_h
#define yfs_client_h

#include <string>
#include <cstring>
#include "lock_protocol.h"
#include "lock_client.h"

#include "extent_client.h"
#include <vector>
#include <list>
#include <sstream>
#include <fstream>

#define CA_FILE "./cert/ca.pem"
#define USERFILE	"./etc/passwd"
#define GROUPFILE	"./etc/group"
#define ROOT 1
#define MAXBYTE (1 << 20)


class yfs_client {
private:
    extent_client *ec;
    lock_client *lc;

public:
    typedef unsigned long long inum;
    enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST,
                    NOPEM, ERRPEM, EINVA, ECTIM, ENUSE };

    typedef int status;

    typedef struct filestat {
        unsigned long long size;
        unsigned short mode;
        unsigned short uid;
        unsigned short gid;
    } filestat_t;

    typedef struct fileinfo {
        unsigned long long size;
        unsigned int atime;
        unsigned int mtime;
        unsigned int ctime;
        unsigned short mode;
        unsigned short uid;
        unsigned short gid;
    } fileinfo_t;

    struct dirinfo {
        unsigned int atime;
        unsigned int mtime;
        unsigned int ctime;
        unsigned short mode;
        unsigned short uid;
        unsigned short gid;
    };
    struct symlink_info {
        unsigned long long size;
        unsigned int atime;
        unsigned int mtime;
        unsigned int ctime;
        unsigned short mode;
        unsigned short uid;
        unsigned short gid;
    };

    struct dirent {
        std::string name;
        yfs_client::inum inum;
    };
    typedef struct dirent dirent_t;

private:
    typedef enum action {
        ADD,
        // content of file change
                MODIFY,
        // attribute of file change
                CHANGE,
        DELETE,
        LATEST
    } action_t;
    typedef struct commit_entry {
        // optional tag
        std::string tag;
        // offset of the commit located at log file
        unsigned long long offset;
    } commit_entry_t;
    std::vector<commit_entry_t> commits;
    int current_commit;

    typedef struct log_entry {
        action_t type;
        inum ino;
        inum parent;
        std::string filename;
        std::string content_pre;
        std::string content;
        filestat attr_pre;
        filestat attr;
    } log_entry_t;

    typedef struct stat_entry {
        action_t type;
        inum ino;
        inum parent;
        std::string filename;
        std::string last_content;
        filestat last_attr;
    } stat_entry_t;
    std::vector<stat_entry_t> stats;

    std::string commits_file;
    std::string stat_file;
    std::string log_file;
    yfs_client::inum commits_ino;
    yfs_client::inum stat_ino;
    yfs_client::inum log_ino;
    yfs_client::inum history_ino;

    static std::string filename(inum);
    static inum n2i(std::string);
    int add_file_to_dir(uint32_t dir_inode_num, uint32_t file_inode_num, const char *filename);
    int remove_file_from_dir(uint32_t dir, const char *name);
    int remove_file_from_dir(uint32_t dir, inum ino);
    int add_stat(action_t type, inum ino, inum parent, const char *filename, std::string &last_content, filestat *last_attr);
    int stats_read();
    int stats_write();
    void put_string(std::stringstream &ss, std::string &str);
    std::string get_string(std::stringstream &ss);
    void put_attr(std::stringstream &ss, filestat &st);
    void get_attr(std::stringstream &ss, filestat &st);
    void read_log(std::stringstream &ss, std::vector<log_entry_t> &log_list);
    void restore_forward(std::vector<log_entry_t> &log_list);
    void restore_backward(std::vector<log_entry_t> &log_list);
    void reorder_log_list(std::vector<log_entry_t> &log_list);

public:
    yfs_client();
    yfs_client(std::string extent_dst);
    yfs_client(std::string extent_dst, std::string lock_dst);
    yfs_client(std::string extent_dst, std::string lock_dst, const char* cert_file);
    void init();
    ~yfs_client();

    bool isfile(inum);
    bool isdir(inum);
    bool exist(inum ino, const char * file, inum &ino_out);
    extent_protocol::type_t get_file_type(inum i_node_num);

    int getfile(inum, fileinfo &);
    int getdir(inum, dirinfo &);
    int getstat(inum, filestat &);
    int get_link(inum, symlink_info &);

    int setattr(inum, filestat, unsigned long);
    int lookup(inum, const char *, bool &, inum &);
    int create(inum, const char *, mode_t, inum &);

    int create_symlink(inum parent, const char *src, const char *dest, inum &ino_out);
    int read_symlink(inum link_number, std::string & buf_out);

    int readdir(inum, std::list<dirent> &);
    int writedir(inum, std::list<dirent>);
    int readfile(inum, std::string &str);
    int write(inum, size_t, off_t, const char *, size_t &);
    int read(inum, size_t, off_t, std::string &);
    int unlink(inum, const char *);
    int mkdir(inum , const char *, mode_t , inum &);

    /** you may need to add symbolic link related methods here.*/
    int verify(const char* cert_file, unsigned short*);

    action_t get_file_stat(inum ino);

    int commit_current();
    int commit_rollback();
    int stats_rollback();
    int commit_forward();
    int commit_add(unsigned long long offset);
    int commit_next();
    int commit_pre();
    int commits_read();
    int commits_write();

};

#endif 
