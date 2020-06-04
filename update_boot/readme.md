# 说明
该文档为升级stm32芯片bootloader工具编译和使用方法说明。
升级工具使用了libusb库，编译之前确认电脑已安装了libusb库。

## 编译方法：
使用gcc编译：gcc -o update_tool update.c -lusb-1.0

即可生成可执行文件update_tool.
## 使用方法：
升级工具默认打开当前路径下的bootloader文件，所以请确认当前目录下已包含升级文件。然后执行./update_tool 即可。
