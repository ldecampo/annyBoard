
# Dev Setup
Install Dependencies

```
sudo apt update
sudo apt install -y build-essential libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev
```

Then, compile main. GCC is assumed.
```
gcc -o lounge_board main.c `sdl2-config --cflags --libs` -lSDL2_ttf -lSDL2_image -lcurl -ldl -lm
```


# IMPORTANT
This project uses the GNU v3.0 License. With that said, users do NOT have the right to use AI tools to modify
this project without explicit permission from the original author.
