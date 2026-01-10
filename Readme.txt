-------Dragon------
for making python exes harder to reverse engineer 
it was a small university project requested by my Prof not so long ago, and i think 
it does what it should do pretty well
instead of making an obfusticator built inside the program i used the last "free" version of pyarmor which is 6.8.1 , and instead of normal pyinstaller which is cracked in less than a minute it uses nuitka to build the exe, and it applies different anti-debbuging techniques ,blocks most of debbuging-analising-(stealing-your-source-code ) programs , and it Uses UPX as a final layer 
and it is on the heavy anti-debugging side so i think some antiviruses would label the output exe to be a virus as a false positive but i think thats common in this area we are in.
>*usage*<
just run the dependency checker first , it just installs pyarmor 6.8.1,nuitka,cython (it was built used in the previous versions but i got rid of it as it was causing errors , you can consider it a bonus ), msvc c compiler ( it is needed heavily for nuitka onefile ) and UPX for that extra compression 
obviously python should be installed , i mean come on we are protecting it here ;)
and to use Dragon just put the Dragon 1.5.exe in the project directory ( the file that has the .py files you want to build ) , it will ask for that folders name ...just type it 
and it will require the name of your main file ( the one that links everything and the user actually interacts with it )
and see it go. 
--Abdul-Hakim--
