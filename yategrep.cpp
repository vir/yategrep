#include <yatephone.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

class Entry: public TelEngine::NamedList // implies String
{
public:
	enum Type { UNKNOWN = 0, MESSAGE, NETWORK, STARTUP };
public:
	Entry(Type type, const TelEngine::String& text)
		: NamedList(text)
		, m_type(type)
		, m_mark(false)
		, m_next(NULL)
	{
	}
	Type type() const
		{ return m_type; }
	bool marked() const
		{ return m_mark; }
	void mark(bool value = true)
		{ m_mark = value; }
	static const char* typeString(Type t)
	{
		switch(t) {
			case UNKNOWN:
				return "unknown";
			case MESSAGE:
				return "message";
			case NETWORK:
				return "network";
			case STARTUP:
				return "startup";
			default:
				return "xxx";
		}
	}
	Entry* next() const
		{ return m_next; }
	void next(Entry* e)
		{ m_next = e; }
private:
	Type m_type;
	bool m_mark;
	Entry* m_next;
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
	void flush()
	{
		m_channels.clear();
		m_newChannels = 0;
		m_addrs.clear();
		m_newAddrs = 0;
	}
	void dump(TelEngine::Stream& out)
	{
		out.writeData("Query params:\n ");
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

class LogBuf
{
public:
	LogBuf(size_t size)
		: m_size(size)
		, m_head(NULL)
		, m_tail(NULL)
		, m_count(0)
		{ }
	~LogBuf()
	{
		if(m_head || m_tail || m_count)
			fprintf(stderr, "EntryBuf destructed with %u entries\n", m_count);
	}
	inline void push(Entry* e)
	{
		if(m_tail) {
			m_tail->next(e);
			++m_count;
		} else {
			m_head = m_tail = e;
			m_count = 1;
		}
		m_tail = e;
	}
	inline Entry* pop()
	{
		if(! m_head)
			return NULL;
		Entry* e = m_head;
		if(!(m_head = e->next()))
			m_tail = NULL;
		--m_count;
		e->next(NULL);
		return e;
	}
	inline Entry* pushpop(Entry* ne)
	{
		if(! ne)
			return pop();
		push(ne);
		return (m_count <= m_size) ? NULL : pop();
	}
	inline Entry* head()
		{ return m_head; }
	inline size_t size() const
		{ return m_size; }
	inline void size(size_t s)
		{ m_size = s; }
	inline size_t count() const
		{ return m_count; }
	inline size_t avail() const
		{ return m_size - count(); }
	Entry* at(size_t index)
	{
		Entry* r = m_head;
		while(r && index) {
			r = r->next();
			--index;
		}
		return r;
	}
	inline bool empty() const
		{ return ! m_head; }
private:
	size_t m_size;
	Entry* m_head;
	Entry* m_tail;
	size_t m_count;
};


class HtmlFilter:public TelEngine::Stream
{
public:
	HtmlFilter(TelEngine::Stream& pipe, bool killNewline = false)
		: unfiltered(pipe)
		, m_killNewline(killNewline)
		{ }
	virtual bool terminate()
		{ return unfiltered.terminate(); }
	virtual bool valid() const
		{ return unfiltered.valid(); }
	virtual int writeData(const void* buffer, int length)
	{
		const char* buf = (const char*)buffer;
		while((buf[length - 1] == '\n' || buf[length - 1] == '\r') && m_killNewline)
			--length;
		int ret = 0;
		while(length) {
			const char* p = buf;
			do {
				switch(*p) {
					case '<':
					case '>':
					case '&':
					case '"':
						break;
					default:
						++p;
						if(--length)
							continue;
						break;
				}
			} while(0);
			ret += unfiltered.writeData(buf, p - buf);
			buf = p;

			if(! length)
				break;
			switch(*buf) {
				case '<':
					ret += unfiltered.writeData("&lt;");
					break;
				case '>':
					ret += unfiltered.writeData("&gt;");
					break;
				case '&':
					ret += unfiltered.writeData("&amp;");
					break;
				case '"':
					ret += unfiltered.writeData("&quot;");
					break;
				default:
					continue;
			}
			++buf;
			--length;
		}
		return ret;
	}
	virtual int readData (void* buffer, int length)
		{ return unfiltered.readData(buffer, length); }
	using TelEngine::Stream::writeData;
	TelEngine::Stream& unfiltered;
private:
	bool m_killNewline;
};

class Writer
{
public:
	Writer(TelEngine::Stream& strm)
		: m_strm(strm)
		, m_xhtml(false)
		, m_context(0)
		, m_showflag(false)
		, m_tailcount(0)
		, m_skipcount(0)
		, m_buf(NULL)
		{ }
	~Writer()
	{
		if(m_skipcount)
			outputSeparator();
		delete m_buf;
	}
	void eat(Entry* entry);
	void xhtml(bool enable)
		{ m_xhtml = enable; }
	void context(unsigned int lines)
	{
		delete m_buf;
		m_context = lines;
		m_buf = lines ? new LogBuf(lines) : NULL;
	}
protected:
	void output(const Entry& e);
	void outputSeparator();
private:
	TelEngine::Stream& m_strm;
	bool m_xhtml;
	unsigned int m_context;
	bool m_showflag;
	unsigned int m_tailcount;
	unsigned int m_skipcount;
	LogBuf* m_buf;
};

class Grep
{
public:
	Grep(size_t backlog = 1000)
		: m_buf(backlog)
		{ }
	void run(Query& query, Parser parser, Writer& writer);
	void flushBuffer(Writer& writer);
private:
	LogBuf m_buf;
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
		m_channels.append(new TelEngine::String(*s), false);
		modified = true;
	}
	/* XXX TODO: check for new addresses */
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

void Grep::run(Query& query, Parser parser, Writer& writer)
{
	Entry* e = NULL;
	while(( e = parser.get() )) {
		if(e->type() == Entry::STARTUP) {
			flushBuffer(writer);
			query.flush();
		}
		if(query.matches(*e)) {
			e->mark();
#if 1 /* DEEP SEARCH */
			if(query.update(*e, true)) {
				bool modified;
				do {
					modified = false;
					for(Entry* t = m_buf.head(); t && !modified; t = t->next()) {
						if(t->marked())
							continue;
						if(! query.matches(*t, true))
							continue;
						t->mark();
						modified = query.update(*t, false);
					}
				} while(modified);
			}
#endif
		}
		e = m_buf.pushpop(e);
		if(e)
			writer.eat(e);
	}
	flushBuffer(writer);
}

void Grep::flushBuffer(Writer& writer)
{
	Entry* e;
	while((e = m_buf.pop()))
		writer.eat(e);
}

void Writer::eat(Entry* entry)
{
	if(entry->marked()) {
		if(! m_showflag && m_skipcount)
			outputSeparator();
		m_showflag = true;
		m_tailcount = 0;
	} else if(! m_context)
		m_showflag = false;

	if(m_buf) {
		entry = m_buf->pushpop(entry);
		if(! entry)
			return;
	}

	if(m_showflag)
		output(*entry);
	else
		++m_skipcount;

	if(m_context) {
		if(entry->marked()) {
			m_tailcount = 0;
		} else {
			if(++m_tailcount == m_context)
				m_showflag = false;
		}
	}
	delete entry;
}

void Writer::output(const Entry& e)
{
	if(m_xhtml) {
		TelEngine::String s("<pre class=\"");
		s << Entry::typeString(e.type());
		if(e.marked())
			s << " marked";
		s << "\">";
		m_strm.writeData(s);
		HtmlFilter(m_strm, true).writeData(e);
		m_strm.writeData("</pre>\n");
	}
	else { // no xhtml
		if(e.marked())
			m_strm.writeData("MARK>>> ");
#if 0
		m_strm.writeData(TelEngine::String((int)e.type()));
		m_strm.writeData(") ");
#endif
		m_strm.writeData(e);
	}
	m_skipcount = 0;
}

void Writer::outputSeparator()
{
	TelEngine::String msg;
	if(m_xhtml)
		msg << "<div class=\"separator\">";
	msg << " ... skipped " << m_skipcount << " log entries ...";
	if(m_xhtml)
		msg << "</div>";
	msg << "\n";
	m_strm.writeData(msg);
	m_skipcount = 0;
}

static void help()
{
	puts("Usage:\n\tyategrep [opts] field=value inputfilename|-");
	puts("Opts:\n\t-h\tthis help\n\t-o fn\tset output to file named fn");
	puts("\t-D\tdump to stderr resulting query object");
	puts("\t-x\t(X)HTML fragment output\n\t-X\tfull HTML document output");
	puts("\t-C nn\tshow nn messages of context before and after each match");
}

const static char* html_header =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<!DOCTYPE html>\n"
	"<html><head>\n"
	"<style type=\"text/css\">\n"
	"pre { margin:0; }\n"
	"pre.message { color:#844; }\n"
	"pre.network { color:#44A; }\n"
	"pre.startup { text-decoration: underline; }\n"
	"pre.marked  { font-weight:bold; padding:3px; background-color:#EFE; }\n"
	"</style>\n"
	"</head><body>\n";
const static char* html_footer =
	"</body></html>\n";


int main(int argc, char* argv[])
{
	const char* outfile = NULL;
	bool dumpquery = false;
	bool fullhtml = false;

	TelEngine::File input;
	TelEngine::File output;
	Parser parser(input);
	Writer writer(output);

	/* parse command-line options */
	++argv; // skip our filename
	while(--argc) {
		if(**argv != '-')
			break;
		switch((*argv)[1]) {
			case 'h':
				help();
				return 0;
			case 'o':
				outfile = *++argv;
				--argc;
				break;
			case 'D':
				dumpquery = true;
				break;
			case 'X':
				fullhtml = true;
			case 'x':
				writer.xhtml(true);
				break;
			case 'C':
				writer.context(atoi(*++argv));
				--argc;
				break;
			default:
				fprintf(stderr, "Unknown command-line option '%s'\n", *argv);
				break;
		}
		++argv;
	}
	if(argc != 2) {
		help();
		return 1;
	}

	/* parse query */
	char* p = *argv;
	p = strchr(p, '=');
	if(! p) {
		fputs("Query argument must be key=value", stderr);
		return 1;
	}
	*p++ = '\0';
	Query query;
	query.params().setParam(*argv, p);
	++argv; --argc;

	/* parse file name(s) */

	if(outfile) {
		output.openPath(outfile, true, false, true, false, true, true, false);
	} else {
		output.attach(1);
	}

	if(0 == strcmp("-", *argv)) {
		input.attach(0);
	} else {
		input.openPath(*argv);
	}

	if(fullhtml)
		static_cast<TelEngine::Stream&>(output).writeData(html_header);

	Grep grep;
	grep.run(query, parser, writer);

	if(fullhtml)
		static_cast<TelEngine::Stream&>(output).writeData(html_footer);

	if(dumpquery) {
		TelEngine::File err(2);
		query.dump(err);
	}
	return 0;
}

