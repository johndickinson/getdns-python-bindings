.. getdns documentation master file, created by
   sphinx-quickstart on Mon Apr  7 17:05:52 2014.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

getdns: Python bindings for getdns
####################################

"getdns" is an implementation of Python language bindings
for the `getdns <http://getdnsapi.net/>`_ API.  getdns is a
modern, asynchronous DNS API that simplifies access to
advanced DNS features, including DNSSEC.  The API
`specification <http://getdnsapi.net/spec/>`_ was
developed by Paul Hoffman.  getdns is built on top of the
getdns implementation developed as a joint project between
`Verisign Labs
<http://labs.verisigninc.com/en_US/innovation/verisign-labs/index.xhtml>`_
and `NLnet Labs <http://nlnetlabs.nl/>`_.

We have tried to keep this interface as Pythonic as we can
while staying true to the getdns architecture.  With this 
release we are moving towards a design that is more consistent with
Python object design.

Dependencies
============

This version of getdns has been built and tested against Python
2.7.  We also expect these other prerequisites to be
installed:

* `libgetdns <http://getdnsapi.net/>`_, version 0.1.7 or later
* `libldns <https://www.nlnetlabs.nl/projects/ldns/>`_,
  version 1.6.11 or later
* `libunbound
  <http://www.nlnetlabs.nl/projects/unbound/>`_, version
  1.4.16 or later
* `libexpat <http://expat.sourceforge.net/>`_ (needed for unbound)
* `libidn <http://www.gnu.org/software/libidn/>`_ version 1
* `libevent <http://libevent.org/>`_ version 2.0.21 stable

n.b.: libgetdns *must* be built with the libevent extension,
as follows:
::

  ./configure --with-libevent

To enable the use of edns cookies in the Python bindings,
you must compile support for them into libgetdns, by
including the --enable-draft-edns-cookies argument to
configure.

This release has been tested against libgetdns 0.3.1.

Building
========

The code repository for getdns is available at:
`<https://github.com/getdnsapi/getdns-python-bindings>`_.  If you are building from source you will
need the Python development package for Python 2.7.  On
Linux systems this is typically something along the lines of
"python-dev" or "python2.7-dev", available through your
package system.  On Mac OS we are building against the
python.org release, available in source form `here
<https://www.python.org/download/releases/2.7.4>`_.

For the actual build, we are using the standard Python
`distutils <https://docs.python.org/2/distutils/>`_.  To
build and install:
::

  python setup.py build
  python setup.py install

We've added optional support for draft-ietf-dnsop-cookies.
It is implemented as a getdns extension (see below).  It is
not built by default.  To enable it, you must build
libgetdns with cookies support and add the
``--with-edns-cookies`` to the Python module build
(i.e. ``python setup.py build --with-edns-cookies``).

Using getdns
==============

Contexts
--------

All getdns queries happen within a resolution *context*, and among
the first tasks you'll need to do before issuing a query is
to acquire a Context object.  A context is
an opaque object with attributes describing the environment within
which the query and replies will take place, including
elements such as DNSSEC validation, whether the resolution
should be performed as a recursive resolver or a stub
resolver, and so on.  Individual Context attributes may be
examined directly, and the overall state of a given context can be
queried with the Context.get_api_information() method.

See section 8 of the `API
specification <http://getdnsapi.net/spec/>`_


Examples
--------

In this example, we do a simple address lookup and dump the
results to the screen:

.. code-block:: python

    import getdns, pprint, sys
    
    def main():
        if len(sys.argv) != 2:
            print "Usage: {0} hostname".format(sys.argv[0])
            sys.exit(1)
    
        ctx = getdns.Context()
        extensions = { "return_both_v4_and_v6" :
        getdns.EXTENSION_TRUE }
        results = ctx.address(name=sys.argv[1],
        extensions=extensions)
        if results.status == getdns.RESPSTATUS_GOOD:
            sys.stdout.write("Addresses: ")
    
            for addr in results.just_address_answers:
                print " {0}".format(addr["address_data"])
            sys.stdout.write("\n\n")
            print "Entire results tree: "
            pprint.pprint(results.replies_tree)
        if results.status == getdns.RESPSTATUS_NO_NAME:
            print "{0} not found".format(sys.argv[1])
    
    if __name__ == "__main__":
        main()


In this example, we do a DNSSEC query and check the response:

.. code-block:: python

    import getdns, sys
    
    dnssec_status = {
        "DNSSEC_SECURE" : 400,
        "DNSSEC_BOGUS" : 401,
        "DNSSEC_INDETERINATE" : 402,
        "DNSSEC_INSECURE" : 403,
        "DNSSEC_NOT_PERFORMED" : 404
    }
    
    
    def dnssec_message(value):
        for message in dnssec_status.keys():
            if dnssec_status[message] == value:
                return message
    
    def main():
        if len(sys.argv) != 2:
            print "Usage: {0} hostname".format(sys.argv[0])
            sys.exit(1)
    
        ctx = getdns.Context()
        extensions = { "return_both_v4_and_v6" :
        getdns.EXTENSION_TRUE,
                       "dnssec_return_status" :
                       getdns.EXTENSION_TRUE }
        results = ctx.address(name=sys.argv[1],
        extensions=extensions)
        if results.status == getdns.RESPSTATUS_GOOD:
            sys.stdout.write("Addresses: ")
            for addr in results.just_address_answers:
                print " {0}".format(addr["address_data"])
            sys.stdout.write("\n")
    
            for result in results.replies_tree:
                if "dnssec_status" in result.keys():
                    print "{0}: dnssec_status:
                    {1}".format(result["canonical_name"],
                                                           dnssec_message(result["dnssec_status"]))
    
        if results.status == getdns.RESPSTATUS_NO_NAME:
            print "{0} not found".format(sys.argv[1])
    
    
    if __name__ == "__main__":
        main()
        

Known issues
============

* "userarg" currently only accepts a string.  This will be
  changed in a future release, to take arbitrary data types


    
Contents:

.. toctree::
   :maxdepth: 1

   functions
   response
   exceptions


Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

