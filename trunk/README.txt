Work in progress...

//------------------------------------------------------------------------------

Outstanding issues stack:

* Make all database data and index files format to contain version numbers (will help in conversion/transition process in future if/when users need to keep previous data, even for the beta-level releases).

* Ability to regenerate indicies from underlying raw data, using whatever resolution needed.

* Index-refactoring to make it more flexible in terms of used resolution. 

* Write up mechanims (could well be synchronous -- nice and easy) for data
recovery upon server's startup (enumerate existing data directory and check if
data is corrupted and see what to do about it)... or could make it into a
utility of sorts.

* Finish-up the performance TODO -- the get_next_message in range.h already
references get_back_msg and should return it in the same cals to the 'get next
message' (otherwise double access to atomic variable is being issued).

* More comprehensive asio-memory-allocation-for-handlers in more places.

* C++14 generic lambdas (and investigate possible use in aforementioned asio-memory-allocation-for-handlers automatic overloading/selection).

//------------------------------------------------------------------------------

Status:

* core-IO is mostly done (using Boost.Asio) -- using thread-pooled async.
frawemork for best possible scalabality under heavy connections/load scenarios.
Also deploying custom buffer-offest for zero-copy deserialisation of first-few
AMQP header fields (channel and payload size) thus allowing AMQP content-body
frames to be processed with as little protocol-related slowdown as possible.

* AMQP message-parsing is 85% done (using custom-modified -hacked Copernica's
Apache-licensed AMQP-CPP library)

* AMQP server-side protocol implementation is 35% done (connect, tune, open,
basic.publish, basic.consume, close -- enough for something like a 3rd party 
clients to do a basic connection and send/receive some data), so bare-bones 
only at the moment.

* Database (storing topics, indices, etc. on disk in performance-efficient and
scalable way) is 80% done.

* AMQP and database intergartion is 30% done:
1) receiving of AMQP messages on broker's side from a 3rd-party product (e.g.
connected client such as amqp_sendstring from rabbitmq-c, or php's amqplib or
c# rabbitmq lib).
2) storing topic data on our broker's side
3) sending AMQP messagse from broker to a 3rd-party product (e.g. connected
client such as amqp_listen from rabbitmq-c, or relevant calls from php or c#).

//------------------------------------------------------------------------------

* Some (tech/design) notes on the database implementation details:

Boost.Asio has windows support for async file io (but will need to search
performance related issues); but does not support this on Linux et. al.

All of the following options: memory-mapping, open(O_DIRECT | O_NONBLOCK...)
read/write, usual open read/write; will have potential blocking. Memory mapping
due to the kernel file-system page-caching, "O_DIRECT | O_NONBLOCK" because
non-block option does not really apply to files and they will be caused to
delay IO if the underlying disk is too slow.

What is really needed to perform true DMA semantics is the proper in-kernel
async. file IO support. Linux does have this (on O_DIRECT files) but boost.asio
is not yet supporting it...

Some options:

1) We could just go with Windows but it may limit our performance capabilities
down the track (if other, non-file, benefits in using Linux would emerge). It
will also require us to write Windows-explicit (non portable) code a bit.

2) We could allow "temporary file-io pauses" and increase the thread-pool size.
This has the danger however of not scaling too well if "> threads in pool" of
the, massively-numbered, connections end up 'blocking'

3) We could add AIO (true async. file io) in Linux to boost.asio. Thas has the
danger of taking too long to write. Although postponing it till later and
combining with step '1' may have some benefits.

4) We could deploy separate, dedicated, thread(s) to perform all IO and then
doing io_service.post() thereby insuring that no network-io is ever "blocked".

5) There is a "young" library called boost.afio but it may not be really that
relevant (not even sure that it uses native linux-kernel AIO... which would
have to be the whole point of using it)... plus adding yet another lib on what
is already mostly done in boost.asio seems like an overkill. 

So far, we are using step 1 (Windows only):

HANDLE handle = ::CreateFile(...FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED);
::boost::asio::windows::random_access_handle file(my_io_service, handle);
::boost::asio::async_read_at(file, from_offset, buffer(data, bytes), handler(shared_from_this()));

And also calling ::SetFileValidData as simply writing at the end of
file (extending the file size) causes some Windows versions to treat the
relevant WriteFile call as synchronous

and then, if Linux is needed later on, seing whether step 3 is necessary or is
already implemented truncate, fallocate, posix_fallocate could be considered
there is well...

Overall topic-retention and database data-structure implementation
details/thoughts:

Single (sub)directory for a given topic.

One raw data files per topic (each message is of variable length)

Multiple index data files per topic (each tuple is of fixed length), used to
access locations of the raw data file. The indicies are monotonic in nature and
continuously growing.

RAM-wise multiple "range" objects that can read the same topic file, one only
can "write". Each such range represents a memory-mapped window into the raw
data file.

Different threads can access a given range object (syncing via mutex et. al. is
not be needed: there is only  1 writer and multi-readers are implemented together with
SHARED_PTR and atomic value of 'current_size'... .

Lifetime of each of the "range" object is governed by intrusive_ptr as it is
shared by many different objects.

Native Windows support is present (i.e. native Windows executables are being
produced, deployed and tested).

However, currently, MS Visual Studio compiler is not able to compile the C++11
code properly (either the compiler itself crashes when parsing the code, or the
compiler incorrectly derives the pointer to method when such is used for
template non-type args)... Compromising on the C++11 features at this stage
would likely cost us in performance capabilities. 

Solution for the time being appears to simply continue building Windows
programs with other-than-MSVC compilers. MSVC itself may improve as we continue
our work and by the time our production code will see the light of day the
VisStudio may be up to the task. 

Moreover, MSVC compiler may not be the optimal choice anyways -- if we are
going for performance then we may as well eventually try Intel C++ compiler
suite (it may need some VisStudio components but the compiler will be from
Intel).

In the meantime, the existing GCC/MNGW can indeed already produce native
Windows binaries. So we can continue doing what we do best and later purchase
the necessary compiler licenses if need be.
