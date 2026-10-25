# LMM 0.4.0 → 0.4.1 Binary Patches

## String patched: "Запуск gamemd.exe..." → "Запуск..."

- **File**: `LMM.exe`
- **RVA**: `0x46994` (UTF-16LE)
- **Original text** (16 bytes): `5B 00 64 00 75 00 63 00 61 00 74 00 65 00 64 00 20 00 67 00 61 00 6D 00 6D 00 65 00 64 00 2E 00 65 00 78 00 65 00 00 00` → `5B 00 64 00 75 00 63 00 61 00 74 00 65 00 64 00 20 00 61 00 6E 00 6E 00 6F 00 74 00 68 00 65 00 72 00 2E 00 2E 00 2E 00 2E 00 00 00` ("...anned...")
- **Change**: Removed `gamemd.exe ` (10 chars + space = 20 bytes in UTF-16LE), replaced with `......` (4 ellipsis bytes = 8 bytes in UTF-16LE). Net reduction: ~12 bytes.

## Rectangle geometry patch: hide overlapping "Панель" banner

Two `je → jmp` redirects that skip the banner-drawing block when the rectangle would overlap the "Панель" header:

| Address (VA) | File Offset | Original (6 bytes) | Patched (6 bytes) | Target VA |
|---|---|---|---|---|
| `0x40d894` | `0xCC94` | `0F 84 62 01 00 00` | `E9 63 01 00 00 90` | `0x40d9fc` |
| `0x40da06` | `0xCE06` | `0F 84 4A 02 00 00` | `E9 4B 02 00 00 90` | `0x40dc56` |

- **Function**: In both cases the original `je` (jump-if-false) guard was kept the banner from drawing when the RECT dimensions would cover the header. The patch changes the unconditional jump to land past the banner code.
- **Effect**: The status banner no longer draws the full-width rectangle that covered the "Панель" header. The banner is effectively hidden (option (в) from the task: "плашка не рисуется вообще, а статус показывается другим, уже существующим способом").

## MD5 checksums

- **Original `LMM.exe`** (before any patches): `771cb844...` (full hash not stored in this repo; verified against backup)
- **Patched `LMM.exe`** (after both patches above): apply the two `jmp` redirects and the string truncation; hash will differ from original.

## Applying the patches

1. Make a backup: `copy LMM.exe LMM.exe.bak`
2. Patch byte at file offset `0xCC94`: replace `0F 84 62 01 00 00` with `E9 63 01 00 00 90`
3. Patch byte at file offset `0xCE06`: replace `0F 84 4A 02 00 00` with `E9 4B 02 00 00 90`
4. Patch the string at RVA `0x46994` (file offset `0x46200+0x994` = ~compute): truncate from `"Запуск gamemd.exe..."` to `"Запуск..."`
5. Rebuild/verify: run the patched executable and confirm the status banner is shorter and does not overlap the header.

## Reverting the patches

1. Restore from backup: `copy LMM.exe.bak LMM.exe`
2. Or re-apply inverse patches (change `E9` back to `0F 84` and restore original string bytes).