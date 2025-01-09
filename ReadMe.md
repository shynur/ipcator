## 功能

提供 POSIX-compatible 的共享内存分配器与读取器.

## 下载

```bash
git clone -b fast --single-branch git@github.com:shynur/ipcator.git
(cd ipcator
 git checkout `git tag --sort=-creatordate | grep fast - | head -n 1`)
```

### 工具链要求

- Bash, `awk`, `sed`, `grep`.

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

```bash
make ipc
# 或
NDEBUG=1 make ipc
```

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
