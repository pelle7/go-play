# go-play
Retro emulation for the ODROID-GO

2019-06-01 Testversion on feature branch

- Quicksave and quickload
- Speedup toggle

The Volume button is defined differently now.

Holding down the volume button freezes the emulator. Now there are two possibilities:

- Releasing the button activates a simple menu
 - LEFT, RIGHT, button A and the VOLUME button are valid actions
- While holding the button
 - Button B: Saves the quicksave
 - Button A: Loads the quicksave
 - UP: Modifies the volume
 - -> A simple text notification is printed the last action
