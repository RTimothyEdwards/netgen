# Tcl commands to run in the console before netgen is initialized
#
puts stdout "Running NetGen Console Functions"
bind .text <Control-Key-c> {netgen::interrupt}
