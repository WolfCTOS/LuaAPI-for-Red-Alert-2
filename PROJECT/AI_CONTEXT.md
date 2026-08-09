\# Red Alert 2 LuaAPI — AI Project Context



\## 1. Project



Project: LuaAPI for Red Alert 2: Yuri's Revenge



Goal: Create an injectable DLL that adds Lua scripting capabilities on top of the game's existing INI-based configuration and engine functionality.



Long-term showcase goal: Tesla Overload — a gameplay mechanic where a Tesla-related unit can overload enemy buildings/power infrastructure, disable them, and progressively damage them.



Current project stage: rebuilding the project after loss of the previous D:\\ drive contents.



The human project owner does not directly write the C++ implementation or OpenCode prompts. The owner defines goals, desired gameplay behavior, constraints, and evaluates results. LLMs translate those goals into technical plans/prompts. OpenCode performs repository/code/build operations.



Important: do not assume the human owner is an experienced C++ or reverse-engineering developer. Explain important technical decisions and verify assumptions instead of relying on implicit expertise.



\---



\## 2. Platform



Game:

\- Red Alert 2: Yuri's Revenge

\- Windows x86 / Win32



Development environment:

\- Visual Studio 2026 Community

\- MSVC 14.51

\- CMake 4.3

\- Windows SDK 10.x



Project location:



D:\\Games\\Red Alert 2\\LuaAPI



Game location:



D:\\Games\\Red Alert 2



\---



\## 3. Repository Structure



Current intended structure:



LuaAPI/

├── CMakeLists.txt

├── .gitignore

├── include/

│   └── LuaAPI/

├── src/

├── third\_party/

│   ├── YRpp/

│   ├── lua/

│   ├── sol2/

│   └── spdlog/

├── scripts/

└── PROJECT/

&#x20;   └── AI\_CONTEXT.md



third\_party dependencies are Git submodules.



The repository is hosted on GitHub:



https://github.com/WolfCTOS/LuaPI-for-Red-Alert-2



Main branch:



main



\---



\## 4. Dependencies



\### YRpp



Repository:

https://github.com/Phobos-developers/YRpp



Purpose:

\- Reverse-engineered structures and interfaces for Red Alert 2 / Yuri's Revenge.

\- Used as a reference for game classes, layouts, arrays, functions, and offsets.



\### Lua



Version:

5.4.7



\### sol2



Version:

3.3.0



Purpose:

\- C++ ↔ Lua bindings.



\### spdlog



Version:

1.12.0



Purpose:

\- Logging from the injected DLL.



\---



\## 5. Build



Expected Win32 build environment:



```powershell

"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86

