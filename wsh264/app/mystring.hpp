#pragma once

class mystring {
	protected:
		char *m_s;

	public:
		mystring() { m_s = strdup(""); }
		mystring(const mystring &src) {
			m_s = strdup(src.m_s);
		}
		mystring(const char *src) {
			m_s = strdup(src);
		}
		~mystring() {
			free(m_s);
		}
		mystring &operator=(const mystring &src) {
			free(m_s);
			m_s = strdup(src.m_s);
			return *this;
		}
		char *c_str() { return m_s; }
};