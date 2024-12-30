## 下载

```bash
git clone git@github.com:shynur/ipcator.git
```

如果缺少 `<format>` 库 (例如在某些 C++ 编译器版本过低的机器上), 额外执行:

```bash
git submodule init lib/fmt
git submodule update lib/fmt
```

## 试运行

### 在单个进程中测试

```bash
make test
# 或
make clean; NDEBUG=1 make test  # 更好的性能, 更少的日志
```

### 测试双进程间的通信

```bash
make ipc
# 或
make clean; NDEBUG=1 make ipc
```

### 兼容性测试

默认使用 `g++` 编译, 标准为 C++26.
但是可以:

```bash
export CXX=clang++-20  # 替换编译器为 LLVM Clang
export ISOCPP=20  # 使用 C++20
export NDEBUG=1  # 如果不使用 GCC, 那么这一步是必须的!
make clean; make test ipc
```

## 用法示例

见 <./include/tester.hpp>.

## 注意事项

降级 `$ISOCPP` 编译时, **无法实现所有语义** (例如封装性与 const 方法的重载).
