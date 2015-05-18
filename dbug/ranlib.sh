
# Warning - first line left blank for sh/csh/ksh compatibility.  Do not
# remove it.  fnf@Unisoft

# ranlib --- do a ranlib if necessary

if [ -x /usr/bin/ranlib ]
then
	/usr/bin/ranlib $*
elif [ -x /bin/ranlib ]
then
	/bin/ranlib $*
else
	:
fi
