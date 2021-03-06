# Sigmod
A clone of https://github.com/sigsegv-mvm/sigsegv-mvm that anyone can compile and run.
Please note that this extension only works on Linux!!!

# Build Instructions
(Note: These instructions work for Debian/Ubuntu/Linux Mint.)

1. Clone this repository or download a zip of it and extract it.

2. Navigate to this repository's folder on a bash shell and run the following commands:

```
$ cd setup

$ sh ambuild_setup.sh             #NOTE: Skip this step if you already installed ambuild
                                  #(but read the .sh files and change the sdkfolder paths!)
$ sh additional_packages.sh

$ sh build.sh

$ cd ../build/package
```

3. Copy everything in here to your server's /tf/ folder.

4. In /tf/addons/sourcemod/extensions, create a blank file called "sigsegv.autoload":

`$ touch sigsegv.autoload`

5. Start your server. Run the following command on your srcds console to print out all the available cvars:

`find sig_`

and set the cvars you want to enable in your server.cfg.

# Troubleshooting
These steps created a working binary on a blank Linux Mint 20 installation.

The most common loading error is if you compiled against the wrong version of Sourcemod and Metamod. Make sure that your copies of the Sourcemod and Metamod source code (the "sourcemod" and "mmsource-1.10" folders in ~/sdkfolder if you ran the ambuild_setup.sh file) match the same version of Sourcemod and Metamod your server is running! If not, then switch the branch in the sourcemod/mmsource-1.10 folders (using "git switch") to the correct versions and re-run build.sh again.
