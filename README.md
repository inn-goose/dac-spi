# DAC SPI

oscilloscope music
https://www.youtube.com/playlist?list=PLc4EnsriUcfQPomSF3Eh6sB143HE2r0tf
https://drive.google.com/drive/folders/1UHvGC6-TDywFri7am8YJl5G6svK33qhC
500 us


oscilloscope
https://www.batronix.com/files/Rigol/Oszilloskope/DHO800/Manual/DHO800_UserGuide_EN.pdf

Steps to Enable XY Advanced Settings
Navigate to Utility: Press the Utility key on the front panel or tap the utility menu icon on the screen.
Access About: Go to the System tab and select About.
Activate Test Mode: Rapidly tap the screen in the About menu three times. This activates a "Test Mode" or debug mode.
Open XY Mode: Navigate to the Horizontal menu and enable XY mode.
Access Advanced Settings: Open the XY setup window (hamburger menu). The "Advanced Settings" switch will now be available, allowing you to access additional, more detailed controls.



## Arduino

```bash
## compile M7
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 ./dac_spi/dac_spi.ino

## compile M4
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm4 ./dac_spi/dac_spi.ino

## upload M7
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn arduino:mbed_giga:giga:target_core=cm7 ./dac_spi/dac_spi.ino

## upload M4
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn arduino:mbed_giga:giga:target_core=cm4 ./dac_spi/dac_spi.ino
```



## CLI

### init

```bash
pip3 install virtualenv

# get the python version
python3 --version
...
Python 3.14.2

# specify python version here 👇 (use only X.Y part)
PATH=${PATH}:~/Library/Python/3.14/bin/ ./env/init.sh

# activate the newly created venv
source venv/bin/activate

# add the CLI library to the python path
export PYTHONPATH=./dac_spi_cli/:$PYTHONPATH
```

### run

```bash
./dac_spi_cli/cli.py ./samples/32k_tone_500.wav
```



## Arduino

### Json RPC

```json
{"jsonrpc":"2.0", "id":0, "method": "start_player", "params": []}

{"jsonrpc":"2.0", "id":0, "method": "play_frame", "params": [[0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766], [0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766, 0, 32767, 0, -32766]]}
```