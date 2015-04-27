Most non-US ISPs charge heavily for data usage which makes services like Steam expensive (or at worst, infeasible); some ISPs go to the effort of creating [unmetered servers](http://www.telstraclear.co.nz/sub-sites/gaming-steam/) but there's no reliable way of limiting Steam's downloading to only those servers in Steam itself.

This project provides a tool for restricting Steam's use of content servers so that it will only use the designated unmetered server provided by the ISP; at present this supports these ISPs:
  * In New Zealand: Vodafone
  * In Australia: Telstra BigPond, iiNet, Internode, Optus, iPrimus, and Adam (thanks to rules designed by [Angus Wolfcastle](http://www.anguswolfcastle.co.cc/steam))
  * In South Africa: webafrica, Internet Solutions, SAIX and MWeb (along with a special detection system for dual ISPs)
  * In Iceland: Vodafone Iceland (again thanks to [Angus Wolfcastle](http://www.anguswolfcastle.co.cc/steam))

There is a [Google App Engine web service](AppEngine.md) to select the correct ISP at installation time; however, you can also filter using a manually-entered pattern (which can be just an IP address) as an alternative. Note that if you're in an organization like a University that has an arrangement with one of the supported ISPs, your IP might not be detected - however, if you let me know directly or on the [discussion group](https://groups.google.com/group/steam-limiter) it will be easy to add something for you.

This was just something I wrote for myself on the day that [TelstraClear officially offered unmetered support for Steam](http://unmetered.co.nz), as there was no working solution at the time for Windows XP; with Windows 7 it's possible to use [firewall tricks](http://www.anguswolfcastle.co.cc/steam), but the approach taken here is potentially simpler and it doesn't cause Steam any real trouble and more importantly, is implemented via a shim that is easy to unload - which is sometimes necessary for content which has not been replicated to the unmetered mirror servers.

Although Steam Limiter was written for Windows XP, it also runs just fine on all later editions of Windows including Windows 7 and 64-bit editions. Since it's independent of your firewall, it also works even if you are using a third-party firewall. And handily, it uses little memory and is unobtrusive.

The limiter isn't quite perfect; there are a few small [limitations](Limitations.md) to be aware of, although in normal use once the limiter is installed these shouldn't affect you; I'm [open to feedback](http://steam-limiter.appspot.com/feedback) about this - it's possible to make the limiter a little more leakproof, but the techniques to do that have some drawbacks which I'm reluctant to employ.

Adding support for additional ISPs is reasonably simple and I'm quite happy to do that; feel free to file an issue here, e-mail me describing your ISP (and ideally, the IP address used by them for their Steam server), upload a rule via the Profile configuration dialog inside the latest version, or post to the [discussion group](https://groups.google.com/forum/#!forum/steam-limiter).

Note that if you have a Google account, you can star this project (at the top of the column to the left of the page), and you can star any [open enhancement requests](http://code.google.com/p/steam-limiter/issues/list) to be notified when they change status - starring an enhancement also shows that you value it, so starring is a way to express which ones you consider most valuable and therefore deserve to be done first.

Also, if you use an RSS feed reader like [Google reader](http://reader.google.com) you can also use it to subscribe to notifications of updates such as new versions via the [Project Feeds](http://code.google.com/p/steam-limiter/feeds) link.