### tcp loop tests

`
gst-launch-1.0 -e pulsesink ! audioconvert ! tcpserversink port=30000
gst-launch-1.0 -e audiotestsrc is-live=TRUE ! audioconvert ! tcpserversink port=30000
gst-launch-1.0 -e tcpclientsrc port=30000 ! audioparse ! audioconvert ! pulsesink
`

#### theUser2 says

`
It may just bleep for a second or so though
if you replace audiotestsrc is-live=TRUE    with pulsesrc you should get your microphone streamed to your self
So what we'd want to do would be something like unixserversink socket-path=/path/to/unix.socket
everything else should be identical to tcp
so if you socat a that socket to a tcp port.... the tcp plugin should be able to talk to it as it normally would talk to another tcp plugin
`
