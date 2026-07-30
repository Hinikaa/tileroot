Output demo.gif
Set Shell "bash"
Set FontSize 20
Set Width 1000
Set Height 480
Set Theme "Dracula"
Set Padding 20
Set TypingSpeed 60ms

Type "./tileroot dump --pretty"
Enter
Sleep 2.5s

Type "./tileroot dump -o session.json --verbose"
Enter
Sleep 2s

Type "./tileroot restore --dry-run"
Enter
Sleep 3s
