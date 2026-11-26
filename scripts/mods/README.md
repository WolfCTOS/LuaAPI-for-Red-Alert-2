# scripts/mods

Currently the only mod in this repository is `smart_ai` — an AI commander built on top of LuaAPI.

More mods will be added in the future by **NiTeMind**, and this description will be expanded as they arrive.

Research and diagnostic mods were removed together with their custom C++ bindings to keep the API close to the Ares/Phobos approach (INI-driven configuration instead of engine-time Lua scripting).

The active mod set is defined in `scripts/active_mods.txt` (currently: `smart_ai`). The loader (`scripts/init.lua`) loads only the mods listed there.
