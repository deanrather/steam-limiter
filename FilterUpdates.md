# The "Keep Updated" Option #

Inside the new filter profile pages in Steam Limiter v0.5 (which have replaced the simple server-selection dialog from v0.4.1 and earlier) is a "Keep Updated" option; this option is off by default, but it's there as an opt-in facility you can use.

# What It Does #

Now that the [rule system in Steam Limiter](FilterRules.md) is more sophisticated and can better-support the more complex filtering arrangements used by Australian ISPs, it's also more likely that the rules will need to evolve and improve over time.

The "Keep Updated" option is a companion facility to the new-version detection in Steam Limiter, which aims to check for new versions of the program roughly every 30 days and make it easy to upgrade.

The same [webservice](http://steam-limiter.appspot.com) that advertises the existence of a new version of the Steam Limiter program is also the one that the Steam Limiter installer calls upon to detect which of the supported ISPs you connecting to the Internet through, and delivers the correct filter rule to you automatically.

If the "Keep Updated" option is checked for your active profile, then when the program looks to see if an updated version exists, it will automatically update the rules used in the Steam Limiter program to the latest ones.

This is essentially the same thing that the manual "Auto-detect" button on the profile page does, but if you tick this checkbox it will do it automatically on the same 30-day cycle as the check for updated versions.
You don't have to tick this (and it's off by default), but it's there if you would like to use it instead of looking for rule updates manually.

# About Rule Updates #

Since the source for the [App Engine](http://appengine.com) webservice is also [included in this Google Code project](http://code.google.com/p/steam-limiter/source/browse/updateapp/main.py#171), you can also review the rules there and see changes being posted to the webserver files.

To suggest a rule change (which includes adding support for a new ISP) the best process is to create a Custom profile with the rule you want and then use the Upload button from the profile page. This generates the same result as [filling out this form](http://steam-limiter.appspot.com/uploadrule) and anonymously records your suggestion in the webservice database.

It's also possible to suggest rule changes by [filing an issue](http://code.google.com/p/steam-limiter/issues/list) or contacting the [program's author](http://profiles.google.com/nigel.bree/about) or using the plain [feedback form](http://steam-limiter.appspot.com/uploadrule), but uploading a specific rule is ideal if you happen to know exactly what you would like.