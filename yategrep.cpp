#include <yatephone.h>

#include <stdio.h>
#include <string.h>
//#include <unistd.h>
//#include <stdlib.h>

class Entry: public TelEngine::NamedList // implies String
{
public:
	enum Type { UNKNOWN = 0, MESSAGE, NETWORK, STARTUP };
public:
	Entry(Type type, const TelEngine::String& text)
		: NamedList(text)
		, m_type(type)
		, m_mark(false)
	{
	}
	Type type() const
		{ return m_type; }
	bool marked() const
		{ return m_mark; }
	void mark(bool value = true)
		{ m_mark = value; }
private:
	Type m_type;
	bool m_mark;
};

class Query
{
public:
	Query()
		: m_params("QueryParams")
	{
	}
	TelEngine::NamedList& params()
		{ return m_params; }
	const TelEngine::NamedList& params() const
		{ return m_params; }
	bool matches(const Entry& e, bool partial = false) const;
	bool update(const Entry& e, bool reset); /**< Updates query with new channels and addresses from log entry. @return true if query was really modified */
	void dump(TelEngine::Stream& out)
	{
		out.writeData("Params:\n ");
		TelEngine::String d;
		m_params.dump(d, " ", '\'', true);
		out.writeData(d);

		d = "\nChannels(";
		d << m_channels.count() << "/" << m_channels.length() << "):\n";
		for(TelEngine::ObjList* p = m_channels.skipNull(); p; p = p->skipNext()) {
			d << " " << p->get()->toString();
		}
		out.writeData(d);
		out.writeData("\nAddresses:\n");
		for(TelEngine::ObjList* p = m_addrs.skipNull(); p; p = p->skipNext()) {
			out.writeData(" ");
			out.writeData(p->toString());
		}
		out.writeData("\n");
	}
private:
	TelEngine::NamedList m_params;
	TelEngine::ObjList m_channels;
	unsigned int m_newChannels;
	TelEngine::ObjList m_addrs;
	unsigned int m_newAddrs;
};

class Parser
{
	const static size_t m_bufsize = 8192;
public:
	Parser(TelEngine::Stream& strm)
		: m_stream(strm)
		, m_bufuse(0)
		, m_last(NULL)
		, m_verbatimCopy(false)
	{
	}
	Entry* get();
protected:
	TelEngine::String getLine(int eol = '\n');
	Entry* parseLine(TelEngine::String s);
	inline Entry* setLast(Entry* e)
		{ Entry* tmp = m_last; m_last = e; return tmp; }
private:
	TelEngine::Stream& m_stream;
	char m_buf[m_bufsize];
	size_t m_bufuse;
	Entry* m_last;
	bool m_verbatimCopy;
};

template<class T>
class RingBuf
{
public:
	RingBuf(size_t size)
		: m_size(size), m_put_index(0), m_get_index(0), m_empty(true)
		{ m_buf = new T*[size]; }
	~RingBuf()
		{ delete[] m_buf; }
	void push(T* entry)
	{
		if(m_put_index == m_get_index && ! m_empty)
			return; // XXX overflow XXX
		m_buf[m_put_index++] = entry;
		if(m_put_index >= m_size)
			m_put_index = 0;
		m_empty = false;
	}
	T* pop()
	{
		if(m_empty)
			return NULL;
		T* entry = m_buf[m_get_index++];
		if(m_get_index >= m_size)
			m_get_index = 0;
		if(m_get_index == m_put_index)
			m_empty = true;
		return entry;
	}
	size_t count() const
	{
		if(m_empty)
			return 0;
		else if(m_put_index > m_get_index)
			return m_put_index - m_get_index;
		else
			return m_put_index + m_size - m_get_index;
	}
	size_t avail() const
		{ return m_size - count(); }
	T* at(size_t index)
	{
		if(index >= count())
			return NULL;
		index += m_get_index;
		if(index > m_size)
			index -= m_size;
		return m_buf[index];
	}
	bool empty() const
		{ return m_empty; }
private:
	T** m_buf;
	size_t m_size;
	size_t m_put_index;
	size_t m_get_index;
	bool m_empty;
};

class Grep
{
public:
	Grep(Query& q, size_t backlog = 1000)
		: m_query(q)
		, m_buf(backlog)
	{
	}
	void run(TelEngine::Stream& in, TelEngine::Stream& out);
private:
	void output(const Entry& e, TelEngine::Stream& out);
	Query& m_query;
	RingBuf<Entry> m_buf;
};

static bool isChannelParam(const TelEngine::String& name)
{
	using namespace TelEngine;
	if(name == YSTRING("id"))
		return true;
	if(name == YSTRING("targetid"))
		return true;
	if(name == YSTRING("peerid"))
		return true;
	if(name == YSTRING("lastpeerid"))
		return true;
	if(name == YSTRING("newid"))
		return true;
	if(name == YSTRING("id.1"))
		return true;
	if(name == YSTRING("newid.1"))
		return true;
	if(name == YSTRING("peerid.1"))
		return true;
	return false;
}

static bool fullMatch(const TelEngine::NamedList& key, const TelEngine::NamedList& entry)
{
	unsigned int n = entry.length();
	unsigned int qn = key.length();
#if 0
TelEngine::String d1, d2;
key.dump(d1, " ", '\'', true);
entry.dump(d2, " ", '\'', true);
fprintf(stderr, "Checkong entry %s against key %s\n", d2.c_str(), d1.c_str());
#endif
	for(unsigned int qi = 0; qi < qn; ++qi) {
		TelEngine::NamedString* q = key.getParam(qi);
		if(! q)
			continue;
		bool found = false;
		for(unsigned int i = 0; i < n; ++i) {
			TelEngine::NamedString* s = entry.getParam(i);
			if(! s)
				continue;
			if(s->name() == q->name()) {
				found = true;
				if(*s != *q)
					return false; /* AND logic, fail on first non-equal param */
				else
					break;
			}
		}
		if(! found)
			return false; /* all requested parameters should be here */
	}
	return true;
}

bool Query::matches(const Entry& e, bool partial /* = false */) const
{
	if(!partial) { /* Full match */
		if(e.type() == Entry::MESSAGE && fullMatch(params(), e))
			return true;
	}

	for(TelEngine::ObjList* chans = m_channels + (partial ? m_newChannels : 0); chans; chans = chans->skipNext()) { // check channel names
		TelEngine::GenObject* o = chans->get();
		if(! o)
			continue;
		TelEngine::String chan = o->toString();
		unsigned int n = e.length();
		for(unsigned int i = 0; i < n; ++i) {
			TelEngine::NamedString* s = e.getParam(i);
			if(! s)
				continue;
			if(! isChannelParam(s->name()))
				continue;
			if(*s == chan)
				return true;
		}
	}
	// TODO: check addresses
	return false;
}

/* Updates query with new channels and addresses from log entry. @return true if query was really modified */
bool Query::update(const Entry& e, bool reset)
{
	if(reset) {
		m_newChannels = m_channels.count();
		m_newAddrs = m_addrs.count();
	}
fprintf(stderr, "Query::update called\n");
	bool modified = false;
	unsigned int n = e.length();
	for(unsigned int i = 0; i < n; ++i) {
		TelEngine::NamedString* s = e.getParam(i);
		if(! s)
			continue;
		if(! isChannelParam(s->name()))
			continue;
		if(m_channels.find(*s))
			continue;
fprintf(stderr, "passwd 3ed(%s=%s)\n", s->name().c_str(), s->c_str());
		m_channels.append(new TelEngine::String(*s), false);
		modified = true;
	}
	/* XXX TODO: check for new channels, append, set modified flag */
	return modified;
}

TelEngine::String Parser::getLine(int eol /* = '\n'*/)
{
	TelEngine::String ret;
	do {
		char* p = (char*)memchr(m_buf, eol, m_bufuse);
		if(p) {
			++p;
			size_t len = p - m_buf;
			ret.append(m_buf, len);
			memmove(m_buf, p, m_bufuse - len);
			m_bufuse -= len;
			return ret;
		}
		if(! m_stream.valid())
			break;
		m_bufuse += m_stream.readData(m_buf + m_bufuse, m_bufsize - m_bufuse);
	} while(m_bufuse);
	return TelEngine::String::empty();
}

Entry* Parser::parseLine(TelEngine::String s)
{
	//fprintf(stderr, "Parsing: %s\n", s.c_str());
	const static TelEngine::Regexp re1("^Sniffed \\|^Returned ");
	const static TelEngine::Regexp re2("^  param\\['\\(.*\\)'\\] = '\\(.*\\)'");
	const static TelEngine::Regexp re3("^  param\\['\\(.*\\)'\\] = '\\(.*\\)");
	const static TelEngine::Regexp re4("^-----");
	const static TelEngine::Regexp re5("^\\([0-9\\.]\\+ \\)\\?<[a-zA-Z0-9]\\+:[a-zA-Z0-9]\\+> '.*' \\(sending\\|received\\) .* \\(to\\|from\\) \\([0-9\\.]\\+\\):[0-9]\\+");
	const static TelEngine::Regexp re6("^\\([0-9\\.]\\+ \\)\\?<[a-zA-Z0-9]\\+:[a-zA-Z0-9]\\+> '[a-z]\\+:[0-9\\.]\\+:[0-9]\\+-\\([0-9\\.]\\+\\):[0-9]\\+' \\(received [0-9]\\+ bytes\\|sending code [0-9]\\+\\)");
	const static TelEngine::Regexp re7("^Yate ([0-9]\\+) is starting ");
	if(m_verbatimCopy && m_last) {
		m_last->append(s);
		if(s.matches(re4))
			m_verbatimCopy = false;
		return NULL;
	}
	if(s.matches(re2) && m_last && m_last->type() == Entry::MESSAGE) { // simple key = value
//		fprintf(stderr, "Got param, last: %p, type: %d\n", m_last, m_last ? m_last->type() : -1);
//		fprintf(stderr, " key: %s, value: %s\n", s.matchString(1).c_str(), s.matchString(2).c_str());
		m_last->append(s);
		m_last->setParam(s.matchString(1), s.matchString(2));
		return NULL;
	}
	if(s.matches(re3) && m_last && m_last->type() == Entry::MESSAGE) { // multiline value
		TelEngine::String key = s.matchString(1);
		TelEngine::String value = s.matchString(2);
		TelEngine::String tail = getLine('\'');
//		fprintf(stderr, "multiline key: %s, value: %s, tail: %s\n", key.c_str(), value.c_str(), tail.c_str());
		m_last->append(s);
		m_last->append(tail);
		value += tail;
		m_last->setParam(key, value.substr(0, value.length() - 1));
		return NULL;
	}
	if(s[0] == ' ' && m_last) { // retval && thread
		m_last->append(s);
		return NULL;
	}
	if(s.matches(re1)) {
//		fprintf(stderr, "Got message\n");
		Entry* e = new Entry(Entry::MESSAGE, s);
		e->setParam("ts", s.matchString(1));
		e->setParam("address", s.matchString(4));
		return e;
	}
	if(s.matches(re5) || s.matches(re6)) {
//		fprintf(stderr, "Got network: %s\n", s.c_str());
		return new Entry(Entry::NETWORK, s);
	}
	if(s.matches(re4) && m_last) {
		m_last->append(s);
		m_verbatimCopy = true;
		return NULL;
	}
	if(s.matches(re7)) {
		return new Entry(Entry::STARTUP, s);
	}
//	fprintf(stderr, "Building UNKNOWN: %s\n", s.c_str());
	return new Entry(Entry::UNKNOWN, s);
}

Entry* Parser::get()
{
	Entry* e = NULL;
	TelEngine::String s = getLine();
	if(s.null()) {
		if(m_last)
			return setLast(NULL);
		return NULL; // EOF
	} else {
		while(!(e = parseLine(s))) {
			s = getLine();
			if(s.null())
				break;
		}
		e = setLast(e);
		return e ? e : get();
	}
}

void Grep::run(TelEngine::Stream& in, TelEngine::Stream& out)
{
#if 0 // very simple "/bin/cat"
	char buf[8192];
	int rd;
	while((rd = in.readData(buf, sizeof(buf)))) {
		out.writeData(buf, rd);
	}
#elif 0 // advanced "/bin/cat"
	Parser parser(in);
	Entry* e = NULL;
	while(( e = parser.get() )) {
		out.writeData(*e);
		delete e;
	}
#else
	Parser parser(in);
	Entry* e = NULL;
	while(( e = parser.get() )) {
		if(m_query.matches(*e)) {
			e->mark();
#if 1 /* DEEP SEARCH */
			if(m_query.update(*e, true)) {
				int count;
				do {
					count = 0;
					for(size_t index = 0; index < m_buf.count() - 1; ++index) {
						Entry* e = m_buf.at(index);
						if(!e || e->marked())
							continue;
						if(m_query.matches(*e, true)) {
							e->mark();
							if(m_query.update(*e, false))
								++count;
						}
					}
				} while(count);
			}
#endif
		}
		m_buf.push(e);

		if(m_buf.avail() < 1) {
			e = m_buf.pop();
			output(*e, out);
			delete e;
		}
	}
	while(! m_buf.empty()) {
		e = m_buf.pop();
		output(*e, out);
		delete e;
	}
#endif
}

void Grep::output(const Entry& e, TelEngine::Stream& out)
{
	if(e.marked())
		out.writeData("MARK>>> ");
#if 0
	out.writeData(TelEngine::String((int)e.type()));
	out.writeData(") ");
#endif
	out.writeData(e);
}

int main(int argc, const char* argv[])
{
	Query query;
	query.params().setParam("billid", "1413261902-39");
//	query.params().setParam("nonce", "993d17cc6f3b5f8a35c88b3936d3e0d7.1413253502");
//	query.params().setParam("nodename", "unicorn");
	Grep grep(query);
	TelEngine::File input;
	if(argc == 2) {
		input.openPath(argv[1]);
	} else {
		input.attach(0);
	}

	TelEngine::File output(1);
	grep.run(input, output);
	query.dump(output);
	return 0;
}
