把 ARM 板子对应架构的 libmsc.so 放到本目录。

来源：讯飞 MSC/离线识别 SDK 的 Linux ARM 版本库文件。
常见架构：armv7/armhf（32位）或 aarch64（64位）。

启用构建：
  make ASR=1 IFLYTEK_INC=include IFLYTEK_LIB=libs/arm

运行时动态库加载：
  方式1：将 libmsc.so 拷到可执行文件同目录
  方式2：export LD_LIBRARY_PATH=$PWD/libs/arm:$LD_LIBRARY_PATH
  方式3：拷到 /usr/lib 或 /lib 并运行 ldconfig（需root）

注意：仓库里的 libs/x86 与 libs/x64 不能在 ARM 上使用。