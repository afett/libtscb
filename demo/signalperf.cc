#include <boost/bind.hpp>
#include <tscb/signal>
#include <boost/signal.hpp>
#include <boost/signals/connection.hpp>
#include <sigc++/sigc++.h>

#include <time.h>
#include <sys/time.h>

void test_function(int &arg)
{
	arg++;
}

double timed_run(std::function<void(size_t)> fn)
{
	double t=0.0;
	size_t iterations=1;

	for(;;) {
		struct timeval before, after, delta;
		gettimeofday(&before, 0);
		fn(iterations);
		gettimeofday(&after, 0);
		timersub(&after, &before, &delta);
		t=delta.tv_sec+0.000001*delta.tv_usec;
		if (t>0.5) break;
		iterations=iterations*2;
	}

	return t/iterations*1000000000;
}

class opencoded_test {
public:
	typedef std::list<void (*)(int &) > siglist;
	siglist sig;
	void call(size_t iterations, size_t ncallbacks)
	{
		siglist::iterator conn[ncallbacks];
		for(size_t n=0; n<ncallbacks; n++)
			conn[n]=sig.insert(sig.end(), &test_function);
		int k=0;
		while(iterations--) {
			for(siglist::const_iterator i=sig.begin(); i!=sig.end(); ++i)
				(*i)(k);
		}
		for(size_t n=0; n<ncallbacks; n++)
			sig.erase(conn[n]);
	}
	void connect_disconnect(size_t iterations)
	{
		while(iterations--) {
			siglist::iterator conn=sig.insert(sig.end(), &test_function);
			sig.erase(conn);
		}
	}
};

class functionlist_test {
public:
	typedef std::list<std::function<void (int &)> > siglist;
	siglist sig;
	void call(size_t iterations, size_t ncallbacks)
	{
		siglist::iterator conn[ncallbacks];
		for(size_t n=0; n<ncallbacks; n++)
			conn[n]=sig.insert(sig.end(), &test_function);
		int k=0;
		while(iterations--) {
			for(siglist::const_iterator i=sig.begin(); i!=sig.end(); ++i)
				(*i)(k);
		}
		for(size_t n=0; n<ncallbacks; n++)
			sig.erase(conn[n]);
	}
	void connect_disconnect(size_t iterations)
	{
		while(iterations--) {
			siglist::iterator conn=sig.insert(sig.end(), &test_function);
			sig.erase(conn);
		}
	}
};

class tscb_test {
public:
	tscb::signal<void(int &)> sig;

	void call(size_t iterations, size_t ncallbacks)
	{
		tscb::connection conn[ncallbacks];
		for(size_t n=0; n<ncallbacks; n++)
			conn[n]=sig.connect(&test_function);
		int k=0;
		while(iterations--)
			sig(k);
		for(size_t n=0; n<ncallbacks; n++)
			conn[n].disconnect();
	}

	void connect_disconnect(size_t iterations)
	{
		while(iterations--) {
			tscb::connection conn=sig.connect(&test_function);
			conn.disconnect();
		}
	}
};

class boost_test {
public:
	boost::signal<void(int &)> sig;

	void call(size_t iterations, size_t ncallbacks)
	{
		boost::signals::connection conn[ncallbacks];
		for(size_t n=0; n<ncallbacks; n++)
			conn[n]=sig.connect(&test_function);
		int k=0;
		while(iterations--)
			sig(k);
		for(size_t n=0; n<ncallbacks; n++)
			conn[n].disconnect();
	}
	void connect_disconnect(size_t iterations)
	{
		while(iterations--) {
			boost::signals::connection conn=sig.connect(&test_function);
			conn.disconnect();
		}
	}
};

class sigcpp_test {
public:
	sigc::signal<void, int &> sig;
	void call(size_t iterations, size_t ncallbacks)
	{
		sigc::connection conn[ncallbacks];
		for(size_t n=0; n<ncallbacks; n++)
			conn[n]=sig.connect(sigc::ptr_fun(&test_function) );
		int k=0;
		while(iterations--)
			sig.emit(k);
		for(size_t n=0; n<ncallbacks; n++)
			conn[n].disconnect();
	}
	void connect_disconnect(size_t iterations)
	{
		while(iterations--) {
			sigc::connection conn=sig.connect(sigc::ptr_fun(&test_function));
			conn.disconnect();
		}
	}
};

int main()
{
	const size_t many_calls=10;
	opencoded_test o;
	printf("test              call single  call many(10) connect+disconnect\n");
	printf("open_coded    %15lf%15lf%15lf\n",
		timed_run(boost::bind(&opencoded_test::call, &o, _1, 1)),
		timed_run(boost::bind(&opencoded_test::call, &o, _1, many_calls)),
		timed_run(boost::bind(&opencoded_test::connect_disconnect, &o, _1))
		);
	functionlist_test f;
	printf("function_list %15lf%15lf%15lf\n",
		timed_run(boost::bind(&functionlist_test::call, &f, _1, 1)),
		timed_run(boost::bind(&functionlist_test::call, &f, _1, many_calls)),
		timed_run(boost::bind(&functionlist_test::connect_disconnect, &f, _1))
		);
	tscb_test t;
	printf("tscb          %15lf%15lf%15lf\n",
		timed_run(boost::bind(&tscb_test::call, &t, _1, 1)),
		timed_run(boost::bind(&tscb_test::call, &t, _1, many_calls)),
		timed_run(boost::bind(&tscb_test::connect_disconnect, &t, _1))
		);
	boost_test b;
	printf("boost::signal %15lf%15lf%15lf\n",
		timed_run(boost::bind(&boost_test::call, &b, _1, 1)),
		timed_run(boost::bind(&boost_test::call, &b, _1, many_calls)),
		timed_run(boost::bind(&boost_test::connect_disconnect, &b, _1))
		);
	sigcpp_test s;
	printf("sigc::signal  %15lf%15lf%15lf\n",
		timed_run(boost::bind(&sigcpp_test::call, &s, _1, 1)),
		timed_run(boost::bind(&sigcpp_test::call, &s, _1, many_calls)),
		timed_run(boost::bind(&sigcpp_test::connect_disconnect, &s, _1))
		);
}
