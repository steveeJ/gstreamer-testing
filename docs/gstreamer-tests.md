### UNIX socket plugin test

* Download the custom gst tcp plugin which contains the UNIX socket
  functionality. (64-bit version based on gst-plugins-base-1.4.5)

http://www.datafilehost.com/d/151bcf8f

It is recommended to store it to a new and empty directory, because gstreamer
will try to treat all the existing files as a plugin.

Run the following commands in the directory the file was downloaded to.

* Server
`
gst-launch-1.0 --gst-plugin-path=. --gst-debug=unix*:4 --gst-plugin-spew -e
pulsesrc ! audioconvert ! unixserversink path=./new.sock
`

* Client
`
gst-launch-1.0 --gst-plugin-path=. --gst-debug=unix*:4 --gst-plugin-spew -e
unixclientsrc path=./new.sock ! audioparse ! audioconvert ! pulsesin
`
