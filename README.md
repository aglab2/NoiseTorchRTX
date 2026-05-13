<h1 align="center"> NoiseTorch-ng</h1>
<h3 align="center"> Noise Supression Application for PulseAudio or Pipewire</h3>
<p align="center"><img src="https://raw.githubusercontent.com/noisetorch/NoiseTorch/master/assets/icon/noisetorch.png" width="100" height="100"></p> 


<div align="center">
    
  <a href="">[![Licence][licence]][licence-url]</a>
  <a href="">[![Latest][version]][version-url]</a>
    
</div>

[licence]: https://img.shields.io/badge/License-GPLv3-blue.svg
[licence-url]: https://www.gnu.org/licenses/gpl-3.0
[version]: https://img.shields.io/github/v/release/noisetorch/NoiseTorch?label=Latest&style=flat
[version-url]: https://github.com/noisetorch/NoiseTorch/releases
[stars-shield]: https://img.shields.io/github/stars/noisetorch/NoiseTorch?maxAge=2592000
[stars-url]: https://github.com/noisetorch/NoiseTorch/stargazers/

NoiseTorch-ng is an easy to use open source application for Linux with PulseAudio or PipeWire. RNNoise was replaced Nvidia Linux Audio Effects SDK. Use whichever conferencing or VOIP application you like and simply select the filtered Virtual Microphone as input to torch the sound of your mechanical keyboard, computer fans, trains and the likes.

Don't forget to leave a star ⭐ if this sounds useful to you! 

## Linux AFX SDK installation.

Visit Nvidia page to get [SDK](https://catalog.ngc.nvidia.com/orgs/nvidia/teams/maxine/resources/maxine_linux_audio_effects_sdk) and [models](https://catalog.ngc.nvidia.com/orgs/nvidia/teams/maxine/collections/maxine_linux_audio_effects_sdk_collection/artifacts), You will need a free account on Nvidia website.

You will need to download the following files:
1) `NVIDIA_AFX_SDK_Linux_<VERSION>.tar.gz`. I am using version 1.6.1 because it is the only one that works with my GPU.
2) `afx_dereverb_denoiser_<VERSION>-48k-sm86.zip`. Tool requires 48 kHz and dereverb_denoiser model. To find the correct `sm` version use cmdline - pay attention to 8.6 matching sm86

```
$ nvidia-smi --query-gpu=name,compute_cap --format=csv,noheader
NVIDIA GeForce RTX 3070, 8.6
```

Extract `NVIDIA_AFX_SDK_Linux_<VERSION>.tar.gz` to /opt/maxine-sdk. Expected layout

```
$ ls -la /opt/maxine-afx
total 976
drwxr-xr-x  7 admin admin   4096 May 13 20:46  .
drwxr-xr-x 17 root  root    4096 May 13 20:44  ..
drwxr-xr-x  9 admin admin   4096 Jun 12  2025  docs
drwxr-xr-x  3 admin admin   4096 Jun 12  2025  external
drwxr-xr-x  2 admin admin   4096 Jun 12  2025  models
drwxr-xr-x  4 admin admin   4096 Jun 12  2025  nvafx
-r--r--r--  1 admin admin 956770 Jun 12  2025 'NVIDIA AI Product Agreement.pdf'
-rw-r--r--  1 admin admin   6459 Jun 12  2025  README.md
drwxr-xr-x  7 admin admin   4096 Jun 12  2025  samples
-rw-r--r--  1 admin admin    623 Jun 12  2025  version.h
```

Extract `afx_dereverb_denoiser_<VERSION>-48k-sm86.zip` to `/opt/maxine-afx/models/dereverb_denoiser/models/sm_89`. Note that this exact path is hardcoded.

```
ls -la '/opt/maxine-afx/features/dereverb_denoiser/models/sm_89'
total 38712
drwxr-xr-x 2 admin admin     4096 May 13 20:52 .
drwxr-xr-x 3 admin admin     4096 May 13 20:47 ..
-rw-rw-r-- 1 admin admin 39627584 May 13 12:41 dereverb_denoiser_48k.trtpkg
```

Create a new file `/etc/ld.so.conf.d/maxine-afx.conf` for `ld` to pick up the libraries

```
$ cat /etc/ld.so.conf.d/maxine-afx.conf
/opt/maxine-afx/nvafx/lib
/opt/maxine-afx/external/cuda/lib
/usr/local/lib
```

Run the tool and check journalctl outputs
```
$ journalctl -f
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(output_sample_rate) failed (may be OK): invalid parameter
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(output_sample_rate) failed (may be OK): invalid parameter
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_input_channels) failed (may be OK): invalid parameter
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_output_channels) failed (may be OK): invalid parameter
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_input_channels) failed (may be OK): invalid parameter
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: loading model from /opt/maxine-afx/features/dereverb_denoiser/models/sm_89/dereverb_denoiser_48k.trtpkg ...
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_output_channels) failed (may be OK): invalid parameter
May 13 20:57:53 admin-b550mds3h pulseaudio[31763]: maxine: loading model from /opt/maxine-afx/features/dereverb_denoiser/models/sm_89/dereverb_denoiser_48k.trtpkg ...
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: 'dereverb_denoiser' loaded, frame_size=480 (10.0 ms)
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: audio node 'dereverb_denoiser' created and connected
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: 'dereverb_denoiser' loaded, frame_size=480 (10.0 ms)
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: audio node 'dereverb_denoiser' created and connected
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(output_sample_rate) failed (may be OK): invalid parameter
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_input_channels) failed (may be OK): invalid parameter
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_output_channels) failed (may be OK): invalid parameter
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(output_sample_rate) failed (may be OK): invalid parameter
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_input_channels) failed (may be OK): invalid parameter
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: loading model from /opt/maxine-afx/features/dereverb_denoiser/models/sm_89/dereverb_denoiser_48k.trtpkg ...
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: SetU32(num_output_channels) failed (may be OK): invalid parameter
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: loading model from /opt/maxine-afx/features/dereverb_denoiser/models/sm_89/dereverb_denoiser_48k.trtpkg ...
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: No remapping configured, proceeding nonetheless!
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: 'dereverb_denoiser' loaded, frame_size=480 (10.0 ms)
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: audio node 'dereverb_denoiser' created and connected
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: 'dereverb_denoiser' loaded, frame_size=480 (10.0 ms)
May 13 20:57:54 admin-b550mds3h pulseaudio[31763]: maxine: audio node 'dereverb_denoiser' created and connected
```

## Screenshot

![](https://i.imgur.com/T2wH0bl.png)

Then simply select "Filtered" as your microphone in any application. OBS, Mumble, Discord, anywhere.

![](https://i.imgur.com/nimi7Ne.png)

## Demo

Linux For Everyone has a good demo video [here](https://www.youtube.com/watch?v=DzN9rYNeeIU).

## Features
* Two click setup of your virtual denoising microphone
* A single, small, statically linked, self-contained binary

## Download & Install

[Download the latest release from GitHub](https://github.com/noisetorch/NoiseTorch/releases).

Unpack the `tgz` file, into your home directory.

    tar -C $HOME -h -xzf NoiseTorch_x64_v0.12.2.tgz

This will unpack the application, icon and desktop entry to the correct place.  
Depending on your desktop environment you may need to wait for it to rescan for applications, or tell it to do a refresh now.

With gnome this can be done with:

    gtk-update-icon-cache

You now have a `noisetorch` binary and desktop entry on your system.

Give it the required permissions with `setcap`:

    sudo setcap 'CAP_SYS_RESOURCE=+ep' ~/.local/bin/noisetorch

If NoiseTorch-ng doesn't start after installation, you may also have to make sure that `~/.local/bin` is in your PATH. On most distributions e.g. Ubuntu, this should be the case by default. If it's not, make sure to append

```
if [ -d "$HOME/.local/bin" ] ; then
    PATH="$HOME/.local/bin:$PATH"
fi
```

to your `~/.profile`. If you do already have that, you may have to log in and out for it to actually apply if this is the first time you're using `~/.local/bin`.

#### Uninstall

    rm ~/.local/bin/noisetorch
    rm ~/.local/share/applications/noisetorch.desktop
    rm ~/.local/share/icons/hicolor/256x256/apps/noisetorch.png 

## Troubleshooting

Please see the [Troubleshooting](https://github.com/noisetorch/NoiseTorch/wiki/Troubleshooting) section in the wiki.

## Usage

Select the microphone you want to denoise, and click "Load", NoiseTorch-ng will create a virtual microphone called "Filtered Microphone" that you can select in any application. Output filtering works the same way, simply output the applications you want to filter to "Filtered Headphones".

When you're done using it, simply click "Unload" to remove it again, until you need it next time.

The slider "Voice Activation Threshold" under settings, allows you to choose how strict NoiseTorch-ng should be in only allowing your microphone to send sounds when it detects voice.. Generally you want this up as high as possible. With a decent microphone, you can turn this to the maximum of 95%. If you cut out during talking, slowly lower this strictness until you find a value that works for you.

If you set this to 0%, NoiseTorch-ng will still dampen noise, but not deactivate your microphone if it doesn't detect voice.

Please keep in mind that you will need to reload NoiseTorch-ng for these changes to apply.

Once NoiseTorch-ng has been loaded, feel free to close the window, the virtual microphone will continue working until you explicitly unload it. The NoiseTorch-ng process is not required anymore once it has been loaded.

## FAQs

### Latency

NoiseTorch-ng may introduce a small amount of latency for microphone filtering. The amount of inherent latency introduced by noise supression is 10ms, this is very low and should not be a problem. Additionally PulseAudio currently introduces a variable amount of latency that depends on your system. Lowering this latency [requires a change in PulseAudio](https://gitlab.freedesktop.org/pulseaudio/pulseaudio/-/issues/120).

Output filtering currently introduces something on the order of ~100ms with pulseaudio. This should still be fine for regular conferences, VOIPing and gaming. Maybe not for competitive gaming teams.

### Alternatives

- [noise-suppression-for-voice](https://github.com/werman/noise-suppression-for-voice): Denoising software which uses rnnoise. More complex to configure but offers more options. Requires more use of the terminal.

- [Easy Effects](https://github.com/wwmm/easyeffects): Package which offers a large number of different audio effects such as echo cancellation or noise removal. More complex to configure and only supports PipeWire. Denoising uses rnnoise.

## Building from source

Install the Go compiler from [golang.org](https://golang.org/). And make sure you have a working C++ compiler.

```shell
 git clone https://github.com/noisetorch/NoiseTorch # Clone the repository
 cd NoiseTorch # cd into the cloned repository
 make # build it
```

To install it:

```shell
mkdir -p  ~/.local/bin
cp ./bin/noisetorch ~/.local/bin/
cp ./assets/noisetorch.desktop ~/.local/share/applications
cp ./assets/icon/noisetorch.png ~/.local/share/icons/hicolor/256x256/apps
```

## Special thanks to

* [@lawl](https://github.com/lawl) Creator of NoiseTorch
* [xiph.org](https://xiph.org)/[Mozilla's](https://mozilla.org) excellent [RNNoise](https://jmvalin.ca/demo/rnnoise/).
* [@werman](https://github.com/werman/)'s [noise-suppression-for-voice](https://github.com/werman/noise-suppression-for-voice/) for the inspiration
* [@aarzilli](https://github.com/aarzilli/)'s [nucular](https://github.com/aarzilli/nucular) GUI toolkit for Go.
* [Sallee Design](https://www.salleedesign.com) (info@salleedesign.com)'s Microphone Icon under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)

