# DAC Player

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
export PYTHONPATH=./dac_player_cli/:$PYTHONPATH
```

### run

```bash
./dac_player_cli/cli.py ./samples/32k_tone_500.wav >./dac_player/pcm_samples.h
```