oshfs.c是旧版，
文件块4KB大小，而且是链表写的，所以当文件达到较大的时候掉速很快。。。测大文件写入的时候在几百mb的量级就只剩2mb/s不到了
准备在文件node上加一个尾部地址来跑分作弊（

oshfs_bad.c就是跑分速度比较快的，写入大文件速度很高，并且修复了一些小bug，所以可以完全无视旧版oshfs.c文件

只实现了实例代码中所要求的那些功能，测试了一下把图片、pdf之类的放进去打开没有问题，删除操作也没有问题，ls -al结果也符合预期
