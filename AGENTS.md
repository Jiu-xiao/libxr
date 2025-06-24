# Usage Guide for Agents

This Guide is written in Chinese.

## 范围
这些准则适用于整个存储库，除非被嵌套的 'AGENTS.md' 文件覆盖。 

## 项目介绍

这个项目是一个完全由c++实现的，可以在Linux、RTOS和bare-metal平台上运行的嵌入式软件框架，使用CMake构建。详细信息需要查看 [README.md](./README.md)。

## 命名约定
- 'src/' 下的源代码应遵循项目的类、方法和自由函数的 PascalCase 命名样式。 
- 目录 'driver/' 和 'system/' 包含供应商或平台代码，**不需要**遵循这些命名规则。

## 内存管理
- 许多组件在初始化期间只分配一次内存，并且故意从不释放内存。不要纯粹将释放添加到平衡分配中。

## 运行测试
1. 使用 CMake 进行配置和构建： 
   ```sh
    mkdir build & cd build & cmake -DLIBXR_TEST_BUILD=True ..&& make
   ```
2. 执行测试套件： 
   ```sh
    ./test
   ```
每当进行代码更改时运行测试。