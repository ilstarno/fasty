

Well, back in time, nobody knows what an garabage collector, virtual machine, java, and so on was

Nowadays we use powerful computers who have a lot of energy, skills to develop and moneys from us

That's why I've written Fasty. I think all the web applications in the world should be written in it. World would be much better off.

In Fasty you just compile and link your logic into one executable that responds immediately to HTTP requests, without creating a new thread or - God forbid - process. There's no VM layer (Hello, Java) nor interpreter, nor external modules' dependencies (Hello, Node.js frameworks). Compilation takes about a second (Hello, Java and Node.js again). You get non-blocking Node.js-like or better performance, with Java-like coding simplicity, C-like instant compilation and instant startup, on much cheaper hardware. Spring boot application boots

Why Fasty
Speed − response measured in mili-seconds, compilation around one second, boot in a fraction of a second. Even for simple hosting it's faster than anything else, because all the statics are read into memory on startup.
Safety − nobody can ever see your application logic nor wander through your filesystem nor run scripts. It has build-in protection against most popular attacks.
Small memory footprint − a couple of MB for demo app − can be easily reduced for embedded apps.
Simple coding − straightforward approach, easy to understand even for a beginner programmer (jump to Hello World).
All-In-One − no need to install external modules; Fasty source already contains all the logic required to run the application, including HTTPS handling, session management, JSON objects and RESTful calls.

Your application logic is in fasty_applicationlication.cpp and app_process_req() is the main entry point, called with every client request. After downloading Fasty, there's a third version of Hello World there to help you get on.

Unlike in other frameworks, you don't need to worry about the response header details, like Content-Type or status. It's all handled automatically, however you can overwrite that if you need, using response macros.

If you want to use Fasty as a regular web server, just to serve statics, leave app_process_req() empty and you'll have the fastest web server in the world.

Some more details
Fasty is written in ANSI C in order to support as many platforms as possible and it's C++ compilers compatible. Sample fasty_application.cpp source module can be C as well as C++ code. Typical application code will look almost the same as in any of the C family language: C++, Java or JavaScript.

It is All-In-One solution for writing typical web application — traditional HTML rendering model, SPA or mixed. It handles HTTPS, and anonymous and registered user sessions. Larger applications or those using potentially blocking resources may want to split logic into the set of services talking to the gateway via POSIX queues in an asynchronous manner, using Fasty's ASYNC facility. CALL_ASYNC macros make it as simple as possible.

Requirements
Fasty is being developed around the idea of using as much generic environment as possible. Therefore it requires only three things:

Computer with operating system (Linux / UNIX / Windows),
C/C++ compiler. I recommend GCC (which is known as MinGW on Windows, AFAIK it is also used by CodeBlocks).
Fasty lib and src directories.
Fuss-free deployment and cloud vendor independency means that production machine requires only operating system and fasty_application executable file(s), and optionally database server if your application uses one.

Priorities / tradeoffs
Every project on Earth has them. So you'd better know.

Speed. Usually whenever I get to choose between speed and code duplication, I choose speed.

Speed. Usually whenever I get to choose between speed and saving memory, I choose speed. So there are mostly statics, stack and arrays, only a few of mallocs. For the same reason static files are read only at the startup and served straight from the memory.

User code simplicity. Usually whenever I get to choose between versatility and simplicity, I choose simplicity. 99.999% of applications do not require 10 levels of nesting in JSON. If you do need this, there is selection of libraries to choose from, or you're a beginner like every programmer once was, and you still need to sweat your way to simple and clean code.

Independency. I try to include everything I think a typical web application may need in Fasty engine. If there are external libraries, I try to use most ubiquitous and generic ones, like i.e. OpenSSL and link statically. Of course you may prefer to add any library you want and/or link dynamically, there's nothing in Fasty that prevents you from doing so.

What does deployment mean? If you've written your app in Fasty, it means copying executable file to production machine which has nothing but operating system installed. OK, add jpegs and css. Oh — wait a minute — you prefer to learn how to develop on Kubernetes first, because everyone talks so cool about it... Then I can't help you. I'm actually learning it but only because my organization handles tens or hundreds of thousands requests per second, we have money for servers, development teams, admin teams and my boss made me. If you're Google or Amazon then you definitely need to have something. There is also a hundred or so of other build automation software. Good luck with choosing the right one. And good luck with paying for the infrastructure. One of my priorities was to make Fasty app not needing this at all.

