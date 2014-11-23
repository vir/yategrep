yategrep
========

Yate log file can be a great source of knowledge (especially if msgsniff is
turned on). But it is really hard to read if more than one call is served in
parallel.

## How it works

This tool is aimed to extract particular call's information from YATE's log
file. It works like this:

* finds messages, satisfying initial query
* uses channel ids from found messages to find more messages
* uses addresses from found messages to find network logs

## Usage examples

* $ `yategrep billid=1413261902-12 /var/log/yate | less`
* $ `yategrep -C 5 billid=1413261902-12 /var/log/yate | less -R`
* $ `yategrep -C 5 -X billid=1413261902-12 /var/log/yate > /tmp/yate-call-12.html`


