#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <stdio.h>
#define FILENAMEMAX  255
#define WRN_BLOCK_START 1
#define BLOCK_LENGTH (size_max-sizeof(address))
typedef unsigned long long address;
typedef unsigned long long mem_size;
static const unsigned long long size_all = 1 * 1024 * 1024 * (size_t)1024;
static const unsigned long long size_max = 4 * 1024;
#define block_num  ((1 * 1024 * 1024 * (size_t)1024) /  (4 * 1024))
#define wrn_block_num (block_num / size_max / 8)
struct filenode{
  char filename[FILENAMEMAX+1];
  address content;//指向数据块
  struct filenode* next;//指向下一个filenode的位置
  struct stat st;//这里不是用指针指向st
  address bid;
};



struct block{
  address next;
  char data[4 * 1024-sizeof(address)];
};


void * mem_blocks[block_num];

struct counter_block{
  unsigned long long total_nums;
  unsigned long long used_nums;
};


struct filenode *root;

static struct filenode *get_filenode(const char *name)
{
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            {
            node=node->next;
            }
        else
            return node;
    }
    return NULL;
}

void * new_block(){
  void * ret;
  ret = mmap(NULL, size_max, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  memset(ret, 0, size_max);
  return ret;
}

void mark_block(unsigned long long block_id ){
  //对已经使用过的block进行mark防止之后再次使用,同时给cb已用的+1
  struct counter_block * cb ;
  int mark_id = block_id / 8 / size_max + WRN_BLOCK_START;
  if(block_id < block_num) {
    unsigned int *c = (unsigned int *) mem_blocks[mark_id];
    c[(block_id % (8 * size_max)) / sizeof(unsigned int)] |= ((unsigned int)1)<<(block_id % (8 * size_max)) % sizeof(unsigned int);
  }
  cb = (struct counter_block *) mem_blocks[0];
  cb->used_nums += 1;
}

void unmark_block(unsigned long long block_id ){
//释放block的时候使用
  struct counter_block * cb ;
  int mark_id = block_id / 8 / size_max + WRN_BLOCK_START;
  if(block_id < block_num) {
    unsigned int *c = (unsigned int *) mem_blocks[mark_id];
    c[(block_id % (8 * size_max)) / sizeof(unsigned int)] &= ~((unsigned int)1)<<(block_id % (8 * size_max)) % sizeof(unsigned int);
  }
  cb = (struct counter_block *) mem_blocks[0];
  cb->used_nums -= 1;
}
int get_zero(unsigned long long a)
{
    int i;
    for(i = 0;(a & (((unsigned long long )1)<<63)) != 0;i++)
    {
      a<<1;
    }
    return i;//从0开始计数
}
address find_free_block(){
int i,j;
int found = 0;
unsigned long long *a;
for(i = WRN_BLOCK_START; i <= wrn_block_num + WRN_BLOCK_START; i++)
{
    a = (unsigned long long *) mem_blocks[i];
    for(j = 0; j<(size_max/sizeof(unsigned long long)); j++)
    {
      if(a[j] == 0xffffffffffffffff) {
        found = 1;
        break;
      }
    }
    if(found == 1) break;
}
if(i == wrn_block_num + WRN_BLOCK_START+1) return 0;
return get_zero(a[j]) + 64 * j + size_max * (i-WRN_BLOCK_START);
//这个address 从 0开始
}

void * oshfs_init(){
  struct counter_block * cb ;
  int i;
  /*for (i = 0; i < block_num; i++) {
    mem_blocks[i] = new_block();
  }*/
  mem_blocks[0] = new_block();
  for (i = 0 ; i < wrn_block_num; i++)
  {
    mem_blocks[WRN_BLOCK_START + i] = new_block();
  }
  mark_block(0);
  //mem_blocks[0]用作记录数量
  for (i = 0 ; i < wrn_block_num; i++)
  {
    mark_block(WRN_BLOCK_START + i);
  }
  cb = (struct counter_block *) mem_blocks[0];
  cb->total_nums = block_num;
//  cb->used_nums = 1 + wrn_block_num;
  return NULL;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
  struct filenode *node=root;
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  while(node){
    filler(buf, node->filename, &(node->st), 0);
    node=node->next;
  }
}

static int create_filenode(const char *filename, const struct stat st){
  address a;
  a=find_free_block();
  if(a != 0){
  mark_block(a);
  struct filenode *node;
  mem_blocks[a] = new_block();
  node = (struct filenode *)mem_blocks[a];
  strncpy(node->filename, filename, FILENAMEMAX + 1);
  node->st = st;
  node->content = 0;//默认为0
  node->next = root;
  root = node;
  node->bid = a;
  return 0;
  }
  else{
    printf("no space to use");
    return 1;
  }
}
static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    int i;
    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    i = create_filenode(path + 1, st);
    if(i == 0) return 0;
    else return -errno;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, &(node->st), sizeof(struct stat));
    } else {
        printf("not found");
        ret = -ENOENT;
    }
    return ret;
}
static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

address find_offset(size_t offset,struct filenode *node)
{
  address a = node->content;
  struct block *block_a ;//(struct block *)mem_blocks[a];
  int i;
  if(a == 0)
  {
    address b = find_free_block();
    mem_blocks[b] = new_block();
    mark_block(b);
    node->content = b;
    a = b;
  }
  //此时a指向链表头
  for(i = size_max-sizeof(address) ; i<offset; i += size_max-sizeof(address) )
  {
    block_a = (struct block *)mem_blocks[a];
    if(block_a->next == 0)
    {
      address b = find_free_block();
      mem_blocks[b] = new_block();
      mark_block(b);
      block_a->next = b;
      a = b;
    }
    else a =block_a->next;
  }
  return a;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  struct filenode *node = get_filenode(path);
  if(node == NULL) return -ENOENT;
  struct counter_block * cb = (struct counter_block *)mem_blocks[0];
  address write_address;
  size_t write_size=0;
  size_t begin =sizeof(address) + offset % (size_max-sizeof(address));
  size_t num;
  unsigned long long need_blocks,aval_blocks;
  aval_blocks = cb->total_nums - cb->used_nums;
  need_blocks = (offset + size - node->st.st_size + size_max - 1) / size_max;
  if(need_blocks < aval_blocks) return -errno;

  write_address=find_offset(offset,node);

  while(write_size<size)
  {
    if(size-write_size <= size_max - begin)
    {
      char * dst = (char *)mem_blocks[write_address];
      dst += begin;
      memcpy(dst,buf + write_size,size-write_size);
      write_size = size;
    }
    else{
      char * dst = (char *)mem_blocks[write_address];
      dst += begin;
      memcpy(dst,buf + write_size,size_max-begin);
      write_size +=size_max-begin;
      address next_address=((struct block *)mem_blocks[write_address])->next;
      if(next_address == 0)
      {
        address b = find_free_block();
        mem_blocks[b] = new_block();
        mark_block(b);
        ((struct block *)mem_blocks[write_address])->next = b;
        next_address = b;
      }
      write_address = next_address;
      begin = sizeof(address);
    }
  }
  num = (node->st.st_size < offset)? node->st.st_size: offset;
  node->st.st_size = (node->st.st_size > num + size) ? node->st.st_size : num + size;
  return write_size;
}
void free_block(address bid)
{
  unmark_block(bid);
  munmap(mem_blocks[bid],size_max);
}
static int oshfs_truncate(const char *path, off_t size)
{
  off_t count=0;
  address a;
  struct block * block_a;
  struct filenode *node = get_filenode(path);
  if(node == NULL) return -ENOENT;
  node->st.st_size = size;
  if(node->content == 0 && size == 0) return 0;
  if(node->content == 0 && size != 0)
  {
    address b = find_free_block();
    mem_blocks[b] = new_block();
    mark_block(b);
    node->content = b;
  }
  a = node->content;
  while (count<size) {
    if(size-count<BLOCK_LENGTH) break;
    block_a = (struct block *) a;
    if(block_a->next == 0)
    {
      address b = find_free_block();
      mem_blocks[b] = new_block();
      mark_block(b);
      block_a->next = b;
      a = b;
    }
    else{
      a = block_a->next;
    }
    count += BLOCK_LENGTH;
  }
  block_a = (struct block *) a;
    for(a=block_a -> next; a != 0 ; )
    {
      address b;
      block_a = (struct block *) a;
      b = block_a->next;
      free_block(a);
      a=b;
    }
return 0;
}
static struct filenode *get_next_filenode(const char *name)
{
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            {
            node=node->next;
            }
        else
            return node;
    }
    return NULL;
}
static int oshfs_unlink(const char *path)
{
  int found=0;
  struct filenode *node = root;
  struct block *block_content;
  address content;
  if(strcmp(node->filename, path + 1) == 0)
  {
    root = node->next;
    content = node->content;
    free_block(node->bid);
    found=1;
  }
  while(node->next){
    if(strcmp(node->next->filename, path + 1) != 0)
            {
            node=node->next;
            }
        else
            {
              address bid = node->next->bid;
              content=node->next->content;
              node->next =  node->next->next;
              free_block(bid);
              found=1;
              break;
            }
  }
  if(found == 0) return -ENOENT;
  while(content!=0)
  {
    address b;
    block_content = (struct block *)mem_blocks[content];
    b=block_content->next;
    free_block(content);
    content=b;
  }
  return 0;
}
//下面这个直接把write的改了一下
static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  struct filenode *node = get_filenode(path);
  if(node == NULL) return -ENOENT;
  address read_address;
  size_t read_size=0;
  size_t begin =sizeof(address) + offset % (size_max-sizeof(address));
  size_t num;
  if(offset>node->st.st_size){
    return 0;
  }
  if(offset + size > node->st.st_size)
        size = node->st.st_size - offset;

  read_address=find_offset(offset,node);

  while(read_size<size)
  {
    if(size-read_size <= size_max - begin)
    {
      char * dst = (char *)mem_blocks[read_address];
      dst += begin;
      memcpy(buf + read_size,dst,size-read_size);
      read_size = size;
    }
    else{
      char * dst = (char *)mem_blocks[read_address];
      dst += begin;
      memcpy(buf + read_size,dst,size_max-begin);
      read_size +=size_max-begin;
      address next_address=((struct block *)mem_blocks[read_address])->next;
      if(next_address == 0)
      {
        printf("read出错\n");
        return -errno;
      }
      read_address = next_address;
      begin = sizeof(address);
    }
  }
  return read_size;
}
static const struct fuse_operations op = {
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);
}
