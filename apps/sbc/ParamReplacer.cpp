/*
 * Copyright (C) 2010 Stefan Sayer
 *
 * This file is part of SEMS, a free SIP media server.
 *
 * SEMS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For a license to use the SEMS software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * SEMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ParamReplacer.h"

#include "log.h"
#include "AmSipHeaders.h"
#include "AmUtils.h"

void replaceParsedParam(const string& s, size_t p,
			AmUriParser& parsed, string& res) {
  switch (s[p+1]) {
  case 'u': { // URI
    res+=parsed.uri_user+"@"+parsed.uri_host;
    if (!parsed.uri_port.empty())
      res+=":"+parsed.uri_port;
  } break;
  case 'U': res+=parsed.uri_user; break; // User
  case 'd': { // domain
    res+=parsed.uri_host;
    if (!parsed.uri_port.empty())
      res+=":"+parsed.uri_port;
  } break;
  case 'h': res+=parsed.uri_host; break; // host
  case 'p': res+=parsed.uri_port; break; // port
  case 'H': res+=parsed.uri_headers; break; // Headers
  case 'P': res+=parsed.uri_param; break; // Params
  default: WARN("unknown replace pattern $%c%c\n",
		s[p], s[p+1]); break;
  };
}

string replaceParameters(const string& s,
			 const char* r_type,
			 const AmSipRequest& req,
			 const string& app_param,
			 AmUriParser& ruri_parser, AmUriParser& from_parser,
			 AmUriParser& to_parser) {
  string res;
  bool is_replaced = false;
  size_t p = 0;
  char last_char=' ';
  
  while (p<s.length()) {
    size_t skip_chars = 1;
    if (last_char=='\\') {
      res += s[p];
      is_replaced = true;
    } else if (s[p]=='\\') {
      if (p==s.length()-1)
      res += s[p];
    } else if ((s[p]=='$') && (s.length() >= p+1)) {
      is_replaced = true;
      p++;
      switch (s[p]) {
      case 'f': { // from
	if ((s.length() == p+1) || (s[p+1] == '.')) {
	  res += req.from;
	  break;
	}
	  
	if (from_parser.uri.empty()) {
	  from_parser.uri = req.from;
	  if (!from_parser.parse_uri()) {
	    WARN("Error parsing From URI '%s'\n", req.from.c_str());
	    break;
	  }
	}

	replaceParsedParam(s, p, from_parser, res);

      }; break;

      case 't': { // to
	if ((s.length() == p+1) || (s[p+1] == '.')) {
	  res += req.to;
	  break;
	}

	if (to_parser.uri.empty()) {
	  to_parser.uri = req.to;
	  if (!to_parser.parse_uri()) {
	    WARN("Error parsing To URI '%s'\n", req.to.c_str());
	    break;
	  }
	}

	replaceParsedParam(s, p, to_parser, res);

      }; break;

      case 'r': { // r-uri
	if ((s.length() == p+1) || (s[p+1] == '.')) {
	  res += req.r_uri;
	  break;
	}
	
	if (ruri_parser.uri.empty()) {
	  ruri_parser.uri = req.r_uri;
	  if (!ruri_parser.parse_uri()) {
	    WARN("Error parsing R-URI '%s'\n", req.r_uri.c_str());
	    break;
	  }
	}
	replaceParsedParam(s, p, ruri_parser, res);
      }; break;

#define case_HDR(pv_char, pv_name, hdr_name)				\
	case pv_char: {							\
	  AmUriParser uri_parser;					\
	  uri_parser.uri = getHeader(req.hdrs, hdr_name);		\
	  if ((s.length() == p+1) || (s[p+1] == '.')) {			\
	    res += uri_parser.uri;					\
	    break;							\
	  }								\
									\
	  if (!uri_parser.parse_uri()) {				\
	    WARN("Error parsing " pv_name " URI '%s'\n", uri_parser.uri.c_str()); \
	    break;							\
	  }								\
	  if (s[p+1] == 'i') {						\
	    res+=uri_parser.uri_user+"@"+uri_parser.uri_host;		\
	    if (!uri_parser.uri_port.empty())				\
	      res+=":"+uri_parser.uri_port;				\
	  } else {							\
	    replaceParsedParam(s, p, uri_parser, res);			\
	  }								\
	}; break;							

	case_HDR('a', "PAI", SIP_HDR_P_ASSERTED_IDENTITY);  // P-Asserted-Identity
	case_HDR('p', "PPI", SIP_HDR_P_PREFERRED_IDENTITY); // P-Preferred-Identity

      case 'P': { // app-params
	if (s[p+1] != '(') {
	  WARN("Error parsing P param replacement (missing '(')\n");
	  break;
	}
	if (s.length()<p+3) {
	  WARN("Error parsing P param replacement (short string)\n");
	  break;
	}

	size_t skip_p = p+2;
	for (;skip_p<s.length() && s[skip_p] != ')';skip_p++) { }
	if (skip_p==s.length()) {
	  WARN("Error parsing P param replacement (unclosed brackets)\n");
	  break;
	}
	string param_name = s.substr(p+2, skip_p-p-2);
	// DBG("param_name = '%s' (skip-p - p = %d)\n", param_name.c_str(), skip_p-p);
	res += get_header_keyvalue(app_param, param_name);
	skip_chars = skip_p-p;
      } break;

      case 'H': { // header
	size_t name_offset = 2;
	if (s[p+1] != '(') {
	  if (s[p+2] != '(') {
	    WARN("Error parsing H header replacement (missing '(')\n");
	    break;
	  }
	  name_offset = 3;
	}
	if (s.length()<name_offset+1) {
	  WARN("Error parsing H header replacement (short string)\n");
	  break;
	}

	size_t skip_p = p+name_offset;
	for (;skip_p<s.length() && s[skip_p] != ')';skip_p++) { }
	if (skip_p==s.length()) {
	  WARN("Error parsing H header replacement (unclosed brackets)\n");
	  break;
	}
	string hdr_name = s.substr(p+name_offset, skip_p-p-name_offset);
	// DBG("param_name = '%s' (skip-p - p = %d)\n", param_name.c_str(), skip_p-p);
	if (name_offset == 2) {
	  // full header
	  res += getHeader(req.hdrs, hdr_name);
	} else {
	  // parse URI and use component
	  AmUriParser uri_parser;
	  uri_parser.uri = getHeader(req.hdrs, hdr_name);
	  if ((s[p+1] == '.')) {
	    res += uri_parser.uri;
	    break;
	  }

	  if (!uri_parser.parse_uri()) {
	    WARN("Error parsing header %s URI '%s'\n",
		 hdr_name.c_str(), uri_parser.uri.c_str());
	    break;
	  }
	  replaceParsedParam(s, p, uri_parser, res);
	}
	skip_chars = skip_p-p;
      } break;

      default: {
	WARN("unknown replace pattern $%c%c\n",
	     s[p], s[p+1]);
      }; break;
      };

      p+=skip_chars; // skip $.X      
    } else {
      res += s[p];
    }

    last_char = s[p];
    p++;
  }

  if (is_replaced) {
    DBG("%s pattern replace: '%s' -> '%s'\n", r_type, s.c_str(), res.c_str());
  }
  return res;
}