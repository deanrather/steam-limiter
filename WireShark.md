# Capturing Packet Traces for Diagnostics #

There are a lot of useful tools for diagnosing things with steam-limiter, primarily [DebugView](http://technet.microsoft.com/en-us/sysinternals/bb896647.aspx) from Microsoft which lets you log trace information that steam-limiter writes out.

However, for some things the only source of complete data on what is going on is the [WireShark](http://www.wireshark.org/) packet capture tool, and here are a few tips on using that for steam-limiter.

# What I'm Looking For #

Because what Steam does is different for everyone, I sometimes need two [WireShark](http://www.wireshark.org/) captures; one with steam-limiter enabled, and one with it turned off. Doing this lets me compare the two; the one with filtering off is very important because it sets a baseline - without that, I can't really tell what a filtered capture _should_ look like for you.

So, if you are reporting a bug in steam-limiter, you'll run through the capture process below twice, and save two files; if you call the one with steam-limiter off something like `baseline.pcap` then I'll know which is which when you send them.

# It's best to restart Steam #

The first time after it has been restarted, when Steam does an actual game download the first thing it does is fetch a list of what servers to use; this list is different for almost everyone, and it's one of the most basic things that affects what steam-limiter has to do.

So, it's really helpful to capture that, which means you want to:

  * Exit and restart Steam, to ensure it requests a fresh server list
  * Start [WireShark](http://www.wireshark.org) capturing
  * Start a game download, preferably a small one - there are lots of indie games with small downloads such as [Gunpoint](http://store.steampowered.com/app/206190/) which has a free demo so you don't need to burn much data.
  * Once the actual download starts and Steam shows it's working, you can stop the capture (and the download) after a few seconds.
  * If necessary, you can edit down the capture using the tips below

# Cutting down a capture #

One of the things about [WireShark](http://www.wireshark.org/) captures is that they do tend to include a lot of extraneous stuff. It's a great tool largely _because_ it captures everything, but for my purposes I don't need all the things it may see going on, such as Dropbox syncing or Gmail or what have you.

Most of that kind of thing is encrypted, whereas all of what Steam does is on simple unencrypted HTTP. Wireshark has a "filter" dialog to choose what is displayed, and you can this this filter to cut things down:

  * In the "Filter:" box, enter
> > `(http or dns) and ip`

> and hit "Apply"

  * To save only this data, from the "File..." menu choose "Export Specified Packets..."
  * This opens a save dialog with a "Packet range" option: if the "All packets" and "Displayed" buttons are highlighted, only the packets being shown will be saved, which if you have used the above filter will be the plain unencrypted HTTP traffic only.

If you want you can add extra filter conditions before doing the export if there's HTTP traffic you want to get rid of, but the above should remove most everything except what we're interested in - "chatty" web apps in open browser tabs like GMail tend to use HTTP with encryption, so Wireshark sees that as "SSL" traffic which the above rule will filter out.

If you see a single packet you want to strip out because you're _absolutely positive_ it's not Steam-related, you can right-click on it in the packet list and choose "Ignore packet (toggle)" from the context menu and it should vanish.

Alternatively, you can extend the filter I gave above by adding something like
> `and ip.addr != 127.0.0.1`
(only with the IP address you want to hide) to the end of the WireShark filter and hitting Apply, and all the packets with the listed IP will be hidden.

Once you're happy that what's left is just Steam, use the "Export Specified Packets" option to save only what is left visible.

# Compress 'em up, Upload, then e-mail me a link #

Especially if you've taken more than one capture, put 'em in an archive using a file compression tool like [7-Zip](http://7-zip.org/) and either e-mail them to me or if the file is still too large to e-mail, use a file host and e-mail me a link.

I happen to like to use [OneDrive](https://onedrive.live.com) (formerly SkyDrive) for this, or something similar like [Google Drive](https://drive.google.com/) would work too. These services have generous free limits and allow you to share links to files so that **you** control who can see them, and you can choose when they can be removed (lots of "free" file storage sites tend to delete stuff after only a day or two, or are overly public).