## Get Started

一个简单的示例, 在 reader 进程中执行 writer 进程里的函数
(该示例未必能成功执行, 因为可能运行于 docker 等权限受限的环境中):

```cpp
// writer.cpp
#include "ipcator.hpp"
using namespace literals;

#include <cstring>

extern "C" int shared_fn(int n) { return n * 2 + 1; }  // 要传递的函数.

int main() {
    Monotonic_ShM_Buffer buf;  // 共享内存 buffer.
    auto block = buf.allocate(0x33);  // 向 buffer 申请内存块.
    std::memcpy((char *)block, (char *)shared_fn, 0x33);  // 向内存块写入数据.

    // 这是 block 所在的 POSIX shared memory:
    auto& target_shm = buf.upstream_resource()->find_arena(block);

    // 事先约定的共享内存 vvvvvvvvvvvvvvvv, 用来存放 target_shm 的路径名:
    auto name_passer = "/ipcator.target-name"_shm[248];
    // 将 target_shm 的路径名拷贝到 name_passer 中:
    std::strcpy(std::data(name_passer), target_shm.get_name().c_str());

    auto offset_passer = "/ipcator.msg-offset"_shm[sizeof(std::size_t)];
    (std::size_t&)offset_passer[0] = (char *)block - std::data(target_shm);

    std::this_thread::sleep_for(1s);  // 等待 reader 获取消息.
}
```

```cpp
// reader.cpp
#include "ipcator.hpp"
using namespace literals;

int main() {
    std::this_thread::sleep_for(0.3s);  // 等 writer 先创建好消息.

    ShM_Reader rd;
    auto& mul2_add1 = rd.template read<int(int)>(
        std::data(-"/ipcator.target-name"_shm),  // 目标共享内存的路径名.
        (std::size_t&)(-"/ipcator.msg-offset"_shm)[0]  // 消息的偏移量.
    );  // 获取函数.

    std::this_thread::sleep_for(1.3s);  // 这时 writer 进程已经退出了, 当我们仍能读取消息:
    std::cout << "\n[[[ 42 x 2 + 1 = " << mul2_add1(42) << " ]]]\n\n\n";
}
```

你可自己手动编译执行; 也可根据 [测试双进程间的通信](#测试双进程间的通信) 的提示,
将以上两段代码分别填到 [`src`](./src/) 目录下的 `ipc-*.cpp` 文件中,
再在仓库目录用 `NDEBUG=1 CXX=g++-10 ISOCPP=20 make ipc` (自己调整 `CXX` 和 `ISOCPP`) 自动执行.

## 功能

提供 POSIX-compatible 的共享内存分配器与读取器.

**执行 `make doc` 以生成文档 (在 `docs/html/`).**

## 下载

```bash
git clone -b master --single-branch --recurse-submodule https://github.com/shynur/ipcator.git
(cd ipcator
 git checkout `git tag --sort=-creatordate | grep [0-9]\$ - | head -n 1`)
```

### 工具链要求

- Bash, `awk`, `sed`, `grep`.

- Doxygen, Graphviz.

- `g++-10` 及以上, 或 `clang++-16` 及以上.

- 直接执行 `make print-vars`, 不飘红就行; 否则, 根据提示设置正确的 编译器 和 C++ 标准.  <br />
  例如, 在我的机器上有报错: `g++: error: unrecognized command line option ‘-std=c++26’; did you mean ‘-std=c++2a’?`,
  说明编译器 (`g++-9`) 太旧导致有些选项无法识别, 而 C++ 标准太新, 可以折中一下, 重新执行

  ```bash
  CXX=g++-10 ISOCPP=20 make print-vars
  ```

  (⚠: `clang++` 在编译时可能会 crash (llvm/llvm-project#113324).  但经过测试, `clang++-19` 是没有问题的.)

### 依赖项

IPCator 抗拒使用第三方库,
除了 同样是 mono-header-only 的库 和 某些标准库的前身:

#### `<format>` ➡️ `fmt/format.h`

C++20 开始提供 `<format>`, 但 `g++-10 -std=c++20` 实际只支持部分新特性.

通过执行 `make print-vars | grep LIBS -` 查看 `LIBS` 变量中是否包含 `fmt`.
如果*是*, 说明本地缺少 `<format>` 库, 需要额外执行:

```bash
git submodule init lib/fmt
git submodule update lib/fmt
```

后续的 make 会在必要时编译生成 `libfmt.a`.

## 试运行

### 在单个进程中测试

```bash
make test
# 或
NDEBUG=1 make test  # 更好的性能, 更少的日志
```

### 测试双进程间的通信

将代码填入 [`src/ipc-writer.cpp`](./src/ipc-writer.cpp)
和 [`src/ipc-reader.cpp`](./src/ipc-reader.cpp), 然后

```bash
make ipc
# 或
NDEBUG=1 make ipc
```

就能看到结果.

### 兼容性测试

默认使用 `g++` 编译, 标准为 C++26.
但是可以:

```bash
export CXX=clang++-20  # 替换编译器为 LLVM Clang
export ISOCPP=20  # 使用 C++20
export NDEBUG=1  # 如果不使用 GCC, 那么这一步是必须的!
# 这 ^^^^^^^^^^^ 会排除许多独属 GCC 而 Clang 无法识别的 debug 选项.
make clean  # 删除用原来的编译器生成的链接库 e.g. `libfmt`.
make test ipc
```

## 用法

> 直接 <code>#include "<a href="./include/ipcator.hpp">ipcator.hpp</a>"</code>.

可公开的 API 默认置于 *global namespace*.
自己到头文件中用期望的 namespace 块包裹住主体代码即可.

### API 使用示例

详见 [`include/tester.hpp`](./include/tester.hpp).

## 注意事项

降级 [`$ISOCPP`](###### "-std=c++$ISOCPP") 编译时, **无法实现所有语义** (例如封装性与 *const* 方法的重载).  <br />
特别是, 形如 异质查找 ([P0919R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r3.html)) 等新的 STL 算法可能会被手工编写的代码代替, 严重降低性能.

## Misc

`.vscode/c_cpp_properties.json` 文件是我自个儿用的.
