yategrep
========

Yate log file can be a great source of knowledge (especially if msgsniff is
turned on). But it is really hard to read if more than one call is served in
parallel.

This tool is aimed to extract particular call's information from YATE's log
file. It works like this:

* finds messages, satisfying initial query
* uses channel ids from found messages to find more messages
* uses ip addresses from found messages to find network logs

