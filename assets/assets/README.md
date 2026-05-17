# companion/assets

Static assets used by PyInstaller when building `IONITY_Companion.exe`.

## Files

| File | Description |
|------|-------------|
| `ionity.ico` | Multi-resolution Windows icon (16–256 px). **Generated** — not committed. |
| `gen_icon.py` | Script that draws and writes `ionity.ico` using Pillow. |

## Generating the icon

Run once before executing `build.bat`:

```powershell
cd companion
pip install Pillow
python assets/gen_icon.py
# → assets/ionity.ico written
```

`ionity.ico` is excluded from version control (see root `.gitignore`).
