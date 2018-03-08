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

yfs_client::yfs_client() {
    ec = NULL;
    lc = NULL;
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

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst, const char* cert_file) {
    ec = new extent_client(extent_dst);
    lc = new lock_client(lock_dst);
    if (ec->put(1, "") != extent_protocol::OK) {
        perror("error init root dir"); // XYB: init root dir
    }

    /*
    this->commits_file = std::string("commits");
    this->stat_file = std::string("stats");
    this->log_file = std::string("log");

    init();
     */
}

yfs_client::~yfs_client() {

}

void yfs_client::init() {
    this->current_commit = -1;
    this->commits.clear();
    this->stats.clear();
    inum ino;
    if (!exist(ROOT, ".history", ino)) {
        mkdir(ROOT, ".history", 777, ino);
    }
    history_ino = ino;

    if (!exist(history_ino, commits_file.c_str(), ino)) {
        create(history_ino, commits_file.c_str(), 777, ino);
    }
    commits_ino = ino;

    if (!exist(history_ino, stat_file.c_str(), ino)) {
        create(history_ino, stat_file.c_str(), 777, ino);
    }
    stat_ino = ino;

    if (!exist(history_ino, log_file.c_str(), ino)) {
        create(history_ino, log_file.c_str(), 777, ino);
    }
    log_ino = ino;
}

int yfs_client::verify(const char *cert_file, unsigned short *uid) {
    int ret = OK;

    return ret;
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
        return true;
    }
    return false;
}

bool yfs_client::isdir(inum i_node_num)
{
    extent_protocol::attr i_node_attr;

    if (ec->getattr(i_node_num, i_node_attr) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    return i_node_attr.type == extent_protocol::T_DIR;
}

bool yfs_client::exist(inum ino, const char * file, inum &ino_out) {
    bool found = false;
    this->lookup(ino, file, found, ino_out);
    return found;
}

extent_protocol::type_t yfs_client::get_file_type(inum i_node_num) {
    extent_protocol::attr i_node_attr;

    if (ec->getattr(i_node_num, i_node_attr) != extent_protocol::OK) {
        printf("error getting attr\n");
        return extent_protocol::T_UNKNOWN;
    }

    return static_cast<extent_protocol::type_t>(i_node_attr.type);
}

int yfs_client::getstat(inum ino, filestat & st) {
    extent_protocol::attr attr;
    if (ec->getattr(ino, attr) != extent_protocol::OK) {
        return -1;
    }

    st.size = attr.size;
    st.uid = attr.uid;
    st.gid = attr.gid;
    st.mode = attr.mode;
}

int yfs_client::getfile(inum ino, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", ino);
    extent_protocol::attr i_node_attr;
    if (ec->getattr(ino, i_node_attr) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = i_node_attr.atime;
    fin.mtime = i_node_attr.mtime;
    fin.ctime = i_node_attr.ctime;
    fin.size = i_node_attr.size;
    fin.mode = i_node_attr.mode;
    fin.uid = i_node_attr.uid;
    fin.gid = i_node_attr.gid;
    printf("getfile %016llx -> sz %llu\n", ino, fin.size);

    release:
    return r;
}

int yfs_client::getdir(inum ino, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", ino);
    extent_protocol::attr a;
    if (ec->getattr(ino, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;
    din.mode = a.mode;
    din.uid = a.uid;
    din.gid = a.gid;

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
    info.mode = a.mode;
    info.uid = a.uid;
    info.gid = a.gid;

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


int yfs_client::setattr(inum ino, filestat st, unsigned long to_set) {
    lc->acquire(ino);

    filestat_t fi;
    /*
    if (get_file_stat(ino) == LATEST) {
        getstat(ino, fi);
    }
    */

    int r = OK;
    std::string buf;
    std::string ls = "";
    fileinfo_t info;
    unsigned long long size = st.size;
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

    extent_protocol::attr attr;
    attr.uid = info.uid;
    attr.gid = info.gid;
    attr.mode = info.mode;
    if (ec->setattr(ino, attr) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    // add_stat(CHANGE, ino, 0, NULL, ls, &fi);


    release:
    lc->release(ino);
    return r;
}

int yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    lc->acquire(parent);
    printf("yfs_client::create: creating %d/%s\n", parent, name);
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
    lc->release(parent);

    add_file_to_dir((uint32_t)parent, (uint32_t)ino_out, name);

    lc->acquire(parent);
    printf("yfs_client::create: created %d/%s\n", parent, name);

    // this->add_stat(ADD, ino_out, parent, name, ls, NULL);

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

    lc->release(parent);
    this->add_file_to_dir(parent, ino_out, dest);
    lc->acquire(parent);

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
    lc->acquire(dir_inode_num);
    lc->acquire(file_inode_num);

    /*
     * std::string buf;
     * this->ec->get(dir_inode_num, buf);

     * std::stringstream ss;
     * ss << file_inode_num << " " << strlen(filename) << " " << filename << " ";
     * buf.append(ss.str());
     * this->ec->put(dir_inode_num, buf);
     */

    std::list<dirent> dir_list;
    this->readdir(dir_inode_num, dir_list);

    dirent_t dir_obj;
    dir_obj.inum = file_inode_num;
    dir_obj.name = std::string(filename);
    bool found = false;
    for (std::list<dirent>::iterator it = dir_list.begin(); it != dir_list.end(); it++) {
        if (it->name.compare(dir_obj.name) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        dir_list.push_back(dir_obj);
    }

    this->writedir(dir_inode_num, dir_list);

    lc->release(file_inode_num);
    lc->release(dir_inode_num);

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
    this->writedir(dir, list);
    std::cout << "!!! done" << std::endl;
    return OK;
}

int yfs_client::add_stat(action_t type, inum ino, inum parent, const char *filename,
                         std::string &last_content, filestat *last_attr) {
    /*
     * skip if the file is the three log file
     */
    if (ino < 6) {
        return -1;
    }

    bool found = false;
    std::vector<stat_entry_t> one_file_stats;
    std::vector<stat_entry_t>::iterator it;
    for (it = stats.begin(); it != stats.end(); it++) {
        if (it->ino == ino && type == it->type) {
            found = true;
            break;
        }
    }

    // if stats do not have entry for ino
    if (!found) {
        stat_entry_t stat;
        stat.type = type;
        stat.ino = ino;
        if (type == ADD) {
            stat.parent = parent;
            stat.filename = filename;
        } else if (type == DELETE) {
            stat.parent = parent;
            stat.filename = filename;
            stat.last_content = last_content;
        } else if (type == MODIFY) {
            stat.last_content = last_content;
        } else {
            stat.last_attr = *last_attr;
        }
        stats.push_back(stat);
    } else {
    }

    stats_write();
    return 0;
}

int yfs_client::stats_read() {
    std::string buffer;
    read(stat_ino, MAXBYTE, 0, buffer);
    std::stringstream ss(buffer);

    stats.clear();
    char ctype;
    while (ss) {
        stat_entry_t stat;
        action_t type;
        ss >> ctype;
        switch (ctype) {
            case 'A':
                type = ADD;
                break;
            case 'M':
                type = MODIFY;
                break;
            case 'C':
                type = CHANGE;
                break;
            case 'D':
                type = DELETE;
                break;
        }

        // ' '
        ss.get();
        ss >> stat.ino;
        ss.get();
        if (type == DELETE || type == ADD) {
            ss >> stat.parent;
            ss.get();
            std::string str;
            stat.filename = get_string(ss);
            if (type == DELETE) {
                ss.get();
                stat.last_content = get_string(ss);
            }
        } else if (type == MODIFY) {
            stat.last_content = get_string(ss);
        } else if (type == CHANGE) {
            ss >> stat.last_attr.uid
               >> stat.last_attr.gid
               >> stat.last_attr.mode;
        }
        ss.get();

        stat.type = type;
        stats.push_back(stat);
    }
}

int yfs_client::stats_write() {
    std::vector<stat_entry_t>::iterator it;
    std::stringstream ss;

    char ctype;
    for (it = stats.begin(); it != stats.end(); it ++) {
        switch (it->type) {
            case ADD:
                ctype = 'A';
                break;
            case MODIFY:
                ctype = 'M';
                break;
            case CHANGE:
                ctype = 'C';
                break;
            case DELETE:
                ctype = 'D';
                break;
            default:
                ctype = 'S';
        }

        // "type ino parent {filename.length} filename\n"
        ss << ctype << " " << it->ino ;
        if (it->type == ADD || it->type == DELETE) {
            ss << ' ' << it->parent << ' ';
            put_string(ss, it->filename);
            if (it->type == DELETE) {
                ss << ' ';
                put_string(ss, it->last_content);
            }
        } else if (it->type == MODIFY) {
            ss << ' ';
            put_string(ss, it->last_content);
        } else if (it->type == CHANGE) {
            ss << ' ' << it->last_attr.uid << ' '
               << it->last_attr.gid << ' ' << it->last_attr.mode;
        }
        ss << '\n';
    }

    std::string buffer = ss.str();
    size_t size = 0;
    write(stat_ino, buffer.length(), 0, buffer.c_str(), size);
}

void yfs_client::put_string(std::stringstream &ss, std::string &str) {
    ss << str.length() << ' ' << str;
}

std::string yfs_client::get_string(std::stringstream &ss) {
    size_t length;
    ss >> length;
    ss.get();

    if (length == 0) {
        return "";
    }

    char s[length + 1];
    ss.get(s, length + 1);

    return std::string(s, length);
}

void yfs_client::put_attr(std::stringstream &ss, filestat &st) {
    ss << st.uid << ' ' << st.gid << ' ' << st.mode;
}

void yfs_client::get_attr(std::stringstream &ss, filestat &st) {
    ss >> st.uid >> st.gid >> st.mode;
}

int yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
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
    lc->release(parent);

    this->add_file_to_dir(parent, ino_out, name);

    lc->release(parent);

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
    std::string buf;
    ec->get(dir, buf);

    if (buf.size() == 0) {
        return OK;
    }

    printf("readdir: buf: %s\n", buf.c_str());
    std::stringstream ss(buf);
    dirent_t dir_obj;

    size_t file_length;
    while (ss >> dir_obj.inum >> file_length) {
        /* skip one space */
        ss.get();

        char tmp[file_length + 1];
        ss.read(tmp, file_length);
        tmp[file_length] = '\0';
        dir_obj.name = std::string(tmp, file_length);
        if (dir_obj.inum > 0) {
            list.push_back(dir_obj);
        }

        /* skip end character '\n' */
        // ss.get();
    }

    return OK;
}

int yfs_client::writedir(inum ino, std::list<dirent> list) {

    std::stringstream ss(" ");
    for (std::list<dirent>::iterator dir_obj = list.begin(); dir_obj != list.end(); dir_obj ++) {
        ss << dir_obj->inum << " " << dir_obj->name.length()
                                  << " " << dir_obj->name << " ";
    }

    printf("writedir: the write buf: \n%s\n", ss.str().c_str());
    if (ec->put(ino, ss.str()) != extent_protocol::OK) {
        std::cerr << "failed to update dir list" << std::endl;
        return IOERR;
    }

    return OK;
}

int yfs_client::readfile(inum ino, std::string &str) {
    size_t size ;
    fileinfo_t ft;
    getfile(ino, ft);
    size = ft.size;
    read(ino, size, 0, str);
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

    size_t length = buf.size();
    if (off >= length) {
        data = "";
    }
    else if (size + off >= length) {
        data = buf.substr(off, length - off);
    } else {
        data = buf.substr(off, size);
    }
    return OK;
}

int yfs_client::write(inum ino, size_t size, off_t off, const char *data,
                  size_t &bytes_written)
{
    lc->acquire(ino);

    /*
    std::string last_content = "";
    if (get_file_stat(ino) == LATEST) {
        readfile(ino, last_content);
    }
     */

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

    // add_stat(MODIFY, ino, 0, NULL, last_content, NULL);

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
    std::string old_content;

    this->lookup(parent, name, found, file);
    if (!found) {
        std::cerr << "file name = " << name << "not found" << std::endl;
        r = NOENT;
        goto release2;
    }

    lc->acquire(file);

    if (isdir(file)) {
        /* currently not support remove dir */
        r = EXIST;
        goto release;
    }

    readfile(file, old_content);

    if (ec->remove(file) != extent_protocol::OK) {
        std::cerr << "error happen while unlinking the file" << std::endl;
        r = IOERR;
        goto release;
    }
    this->remove_file_from_dir(parent, file);
    printf("!!!!unlinking: add stat: filename: %s content: %s, len: %zu\n", name, old_content.c_str(), old_content.size());
    // this->add_stat(DELETE, file, parent, name, old_content, NULL);

    release:
    lc->release(file);
    release2:
    lc->release(parent);
    return r;
}

yfs_client::action_t yfs_client::get_file_stat(inum ino) {
    std::vector<stat_entry_t>::iterator it;
    for (it = stats.begin(); it != stats.end(); it ++) {
        if (it->ino == ino) {
            return it->type;
        }
    }
    return LATEST;
}

int yfs_client::commit_current() {

    // write log base on stats
    printf("committing\n");

    std::vector<stat_entry_t>::iterator it;
    std::string buffer;
    readfile(log_ino, buffer);
    std::stringstream ss(buffer);
    ss.seekp(0, ss.end);
    for (it = stats.begin(); it != stats.end(); it ++) {
        std::string content;
        filestat st;
        switch (it->type) {
            case ADD:
                // "A ino parent {string}
                ss << "A " << it->ino << ' ' << it->parent << ' ';
                put_string(ss, it->filename);
                ss << '\n';
                break;
            case MODIFY:
                readfile(it->ino, content);
                ss << "M " << it->ino << ' ';
                put_string(ss, it->last_content);
                ss << ' ';
                put_string(ss, content);
                ss << '\n';
                break;
            case CHANGE:
                // "C ino uid gid mode\n"
                getstat(it->ino, st);
                ss << "C " << it->ino << ' ';
                put_attr(ss, it->last_attr);
                ss << ' ';
                put_attr(ss, st);
                ss << '\n';
                break;
            case DELETE:
                ss << "D " << it->ino << ' ' << it->parent << ' ';
                put_string(ss, it->filename);
                ss << ' ';
                put_string(ss, it->last_content);
                ss << '\n';
            default:
                break;
        }
    }
    buffer = ss.str();
    size_t written = 0;
    write(log_ino, buffer.size(), 0, buffer.c_str(), written);

    stats.clear();
    stats_write();

    // commit and write commits
    commit_add(buffer.size());

    return 0;
}

int yfs_client::commit_rollback() {
    if (!stats.empty()) {
        stats_rollback();
        return 0;
    }
    // goto previous commit and write commits(update current_commit)
    unsigned long long end = commits[current_commit].offset;
    if (commit_pre() < 0) {
        // already at first commit
        return -1;
    }
    unsigned long long start = commits[current_commit].offset;
    std::cout << "rolling back from " << current_commit + 1 << " to " << current_commit<< std::endl;
    std::string buffer;
    read(log_ino, end - start, start, buffer);
    std::cout << "roll back buffer: " << buffer << "done" << std::endl;
    std::stringstream ss(buffer);

    std::vector<log_entry_t> log_list;
    read_log(ss, log_list);
    restore_backward(log_list);

    stats.clear();
    stats_write();
    return 0;
}

int yfs_client::stats_rollback() {
    std::vector<stat_entry_t>::reverse_iterator it;
    for (it = stats.rbegin(); it != stats.rend(); it++) {
        unsigned long long ino_out;
        size_t written = 0;
        switch (it->type) {
            case ADD:
                unlink(it->ino, it->filename.c_str());
                break;
            case DELETE:
                create(it->parent, it->filename.c_str(), 777, ino_out);
                write(ino_out, it->last_content.size(), 0, it->last_content.c_str(), written);
                break;
            case MODIFY:
                write(it->ino, it->last_content.size(), 0, it->last_content.c_str(), written);
                break;
            case CHANGE:
                setattr(it->ino, it->last_attr, 0);
                break;
        }
    }


    stats.clear();
    stats_write();
}

int yfs_client::commit_forward() {
    unsigned long long start = commits[current_commit].offset;
    // goto next commit and write commit
    if (commit_next() < 0) {
        // failed, already at last commit
        return -1;
    }
    unsigned long long end = commits[current_commit].offset;
    std::string buffer;
    read(log_ino, end - start, start, buffer);
    std::stringstream ss(buffer);

    std::cout << "redo buffer: \n" << buffer << std::endl;

    std::vector<log_entry_t> log_list;
    read_log(ss, log_list);
    restore_forward(log_list);

    stats.clear();
    stats_write();
    return 0;
}

int yfs_client::commit_add(unsigned long long offset) {
    commit_entry_t ct;
    std::vector<commit_entry_t>::iterator it = commits.begin() + current_commit + 1;
    ct.offset = offset;
    commits.insert(it, ct);

    current_commit ++;

    commits_write();
    return 0;
}

int yfs_client::commit_next() {
    if (current_commit + 2 <= commits.size()) {
        current_commit ++;
    } else {
        return -1;
    }

    commits_write();
    return 0;
}

int yfs_client::commit_pre() {
    if (current_commit > 0) {
        current_commit --;
    } else {
        return -1;
    }

    commits_write();
    return 0;
}

int yfs_client::commits_write() {
    std::vector<commit_entry_t>::iterator it;
    std::stringstream ss;
    ss << current_commit << '\n';
    for (it = commits.begin(); it != commits.end(); it ++) {
        // "tag offset\n"
        ss << it->tag.length() << ' ' << it->tag << ' ' << it->offset << '\n';
    }

    std::string buffer = ss.str();
    size_t written = 0;
    write(commits_ino, buffer.size(), 0, buffer.c_str(), written);
}

int yfs_client::commits_read() {
    std::string buffer;
    read(commits_ino, MAXBYTE, 0, buffer);
    std::stringstream ss(buffer);

    ss >> current_commit;
    // '\n'
    ss.get();

    commits.clear();
    size_t length;
    while (ss) {
        commit_entry_t ct;

        ss >> length;
        // ' '
        ss.get();
        char tag[length + 1];
        ss.read(tag, length + 1);
        // ' '
        ss.get();
        ss >> ct.offset;
        // '\n'
        ss.get();

        ct.tag = std::string(tag);

        commits.push_back(ct);
    }
}

void yfs_client::read_log(std::stringstream &ss, std::vector<log_entry_t> &log_list) {
    size_t size = ss.str().size();
    while (ss.tellg() != size) {
        log_entry_t log;
        char ctype;
        ss >> ctype;
        switch (ctype) {
            case 'A': {
                log.type = ADD;
                break;
            }
            case 'D': {
                log.type = DELETE;
                break;
            }
            case 'M': {
                log.type = MODIFY;
                break;
            }
            case 'C': {
                log.type = CHANGE;
                break;
            }
            default:
                break;
        }

        ss.get();
        ss >> log.ino;
        ss.get();
        if (log.type == ADD || log.type == DELETE) {
            ss >> log.parent;
            ss.get();
            log.filename = get_string(ss);
            if (log.type == DELETE) {
                ss.get();
                log.content_pre = get_string(ss);
            }
        } else if (log.type == MODIFY) {
            log.content_pre = get_string(ss);
            ss.get();
            log.content = get_string(ss);
        } else if (log.type == CHANGE) {
            get_attr(ss, log.attr_pre);
            ss.get();
            get_attr(ss, log.attr);
        }
        // '\n'
        char c;
        ss.get(c);
        if (c != '\n') {
            printf("not new line\n");
        }

        log_list.push_back(log);
    }

    std::vector<log_entry_t>::iterator it;
    for (it = log_list.begin(); it != log_list.end(); it++) {
        printf(">>>>log entry info: %llu %llu %s %s %s\n", it->ino, it->parent, it->filename.c_str(), it->content_pre.c_str(), it->content.c_str());
    }
}

void yfs_client::restore_forward(std::vector<log_entry_t> &log_list) {
    reorder_log_list(log_list);

    std::vector<log_entry_t>::iterator it ;
    for (it = log_list.begin(); it != log_list.end(); it ++) {
        inum ino_out;
        size_t written = 0;
        switch (it->type) {
            case ADD:
                if (it->content.size() == 0) {
                    break;
                }
                create(it->parent, it->filename.c_str(), 777, ino_out);
                write(ino_out, it->content.size(), 0, it->content.c_str(), written);
                printf("redo: recreated file: %s inode: %llu\n", it->filename.c_str(), ino_out);
                break;
            case DELETE:
                unlink(it->parent, it->filename.c_str());
                break;
            case MODIFY:
                write(it->ino, it->content.size(), 0, it->content.c_str(), written);
                break;
            case CHANGE:
                setattr(it->ino, it->attr, 0);
                break;
            default:
                break;
        }
    }
}

void yfs_client::restore_backward(std::vector<log_entry_t> &log_list) {
    std::vector<log_entry_t>::reverse_iterator rit;
    for (rit = log_list.rbegin(); rit != log_list.rend(); rit++) {
        std::cout << "rolling back content: inode:" << rit->ino << " parent:" << rit->parent
                  << "filename: "<< rit->filename << " pre content:" << rit->content_pre << std::endl;
        inum ino_out;
        size_t written = 0;

        switch (rit->type) {
            case ADD:
                unlink(rit->parent, rit->filename.c_str());
                printf("deleted filename=%s\n", rit->filename.c_str());
                break;
            case DELETE:
                create(rit->parent, rit->filename.c_str(), 777, ino_out);
                write(ino_out, rit->content_pre.size(), 0, rit->content_pre.c_str(), written);
                printf("recreate filename=%s\n", rit->filename.c_str());
                break;
            case MODIFY:
                write(rit->ino, rit->content_pre.size(), 0, rit->content_pre.c_str(), written);
                printf("rewrite inode=%llu content to %s\n", rit->ino, rit->content_pre.c_str());
                break;
            case CHANGE:
                setattr(rit->ino, rit->attr_pre, 0);
                break;
            default:
                break;
        }
    }
}

void yfs_client::reorder_log_list(std::vector<log_entry_t> &log_list) {
    std::vector<log_entry_t> new_list;
    std::vector<log_entry_t>::iterator it, next;

    for (it = log_list.begin(); it != log_list.end(); it ++) {
        if (it->type != ADD) {
            new_list.push_back(*it);
            continue;
        }

        next = it + 1;
        log_entry_t entry = *it;
        if (next->type == MODIFY && next->ino == entry.ino) {
            entry.content = next->content;
            if ((next+1)->ino == entry.ino && (next+1)->type == DELETE) {
                it += 2;
                continue;
            }
            new_list.push_back(entry);
            it ++;
        } else {
            new_list.push_back(entry);
        }
    }

    log_list = new_list;
    std::cout << "reordered list: " ;
    for (it = log_list.begin(); it != log_list.end(); it++) {
        std::cout << "\n " << it->type << " " << it->ino << " " << it->parent
                  << " " << it->filename << " " << it->content ;
    }
    std::cout << std::endl;
}
