```
git clone https://github.com/hntu1995/pico-w-peripheral-bt-manifest.git
Set-Location .\pico-w-peripheral-bt-manifest
python -m venv .venv
Activate.ps1
pip install -U pip west
west init -l .
west update
west zephyr-export
pip install -r requirements.txt
setup-env.ps1
build.ps1
```
