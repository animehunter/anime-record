#include <ClanLib/core.h>
#include <ClanLib/application.h>
#include <ClanLib/display.h>
#include <ClanLib/gui.h>
#include <ClanLib/database.h>
#include <ClanLib/sqlite.h>
#include <ClanLib/network.h>
#include <algorithm>
#include <numeric>
#include <map>
#include <iterator>

#include "MessageDialog.h"

//#define ENABLE_CONSOLE

// Choose the target renderer
//#define USE_OPENGL_2
//#define USE_OPENGL_1
#define USE_SOFTWARE_RENDERER

#ifdef USE_SOFTWARE_RENDERER
#include <ClanLib/swrender.h>
#endif

#ifdef USE_OPENGL_1
#include <ClanLib/gl1.h>
#endif

#ifdef USE_OPENGL_2
#include <ClanLib/gl.h>
#endif

enum VIEWING_STATUS { UNKNOWN=0, WATCHING=1, COMPLETED=2, ONHOLD=3, DROPPED=4, PLANNING=6 };
enum VIEWING_STATUS_MASK { UNKNOWN_MASK=0, WATCHING_MASK=(1<<1), COMPLETED_MASK=(1<<2), ONHOLD_MASK=(1<<3), DROPPED_MASK=(1<<4), PLANNING_MASK=(1<<6) };

static const int ALL_VIEWING_STATUS_MASK = UNKNOWN_MASK|WATCHING_MASK|COMPLETED_MASK|ONHOLD_MASK|DROPPED_MASK|PLANNING_MASK;

template<class StrType>
StrType trimmed( StrType const& str, char const* sepSet=" \n\r\t")
{
    typename StrType::size_type const first = str.find_first_not_of(sepSet);
    return ( first==StrType::npos) ? StrType() : str.substr(first, str.find_last_not_of(sepSet)-first+1);
}

template<class StrType>
StrType clean (const StrType &oldStr, const StrType &bad) 
{
    typename StrType::iterator s, d;
    StrType str = oldStr;

    for (s = str.begin(), d = s; s != str.end(); ++ s)
    {
        if (bad.find(*s) == StrType::npos)
        {
            *(d++) = *s;
        }
    }
    str.resize(d - str.begin());

    return str;
}

template <typename T, typename Iterator>
T join(
    Iterator b,
    Iterator e,
    const T sep)
{
    T t;

    while (b != e)
    {
        if(b != e-1)
            t = t + *b++ + sep;
        else
            t = t + *b++;
    }

    return t;
}


class DBArg
{
public:
    DBArg(CL_DBConnection &db, const CL_StringRef &format, CL_DBCommand::Type type) : cmd(db.create_command(format, type)), i(1){}

    DBArg &set_arg(const CL_StringRef &arg)
    {
        cmd.set_input_parameter_string(i, arg);
        i++;
        return *this;
    }

    DBArg &set_arg(const char *arg)
    {
        cmd.set_input_parameter_string(i, arg);
        i++;
        return *this;
    }

    DBArg &set_arg(bool arg)
    {
        cmd.set_input_parameter_bool(i, arg);
        i++;
        return *this;
    }

    DBArg &set_arg(int arg)
    {
        cmd.set_input_parameter_int(i, arg);
        i++;
        return *this;
    }

    DBArg &set_arg(double arg)
    {
        cmd.set_input_parameter_double(i, arg);
        i++;
        return *this;
    }

    DBArg &set_arg(const CL_DateTime &arg)
    {
        cmd.set_input_parameter_datetime(i, arg);
        i++;
        return *this;
    }

    DBArg &set_arg(const CL_DataBuffer &arg)
    {
        cmd.set_input_parameter_binary(i, arg);
        i++;
        return *this;
    }

    CL_DBCommand get_result() const
    {
        return cmd;
    }

private:
    CL_DBCommand cmd;
    int i;
};

static DBArg begin_arg(CL_DBConnection &sql, const CL_StringRef &format, CL_DBCommand::Type type)
{
    return DBArg(sql, format, type);
}

template <class Arg1, class Arg2, class Arg3, class Arg4, class Arg5, class Arg6, class Arg7, class Arg8>
CL_DBCommand create_sql_command(CL_DBConnection &sql, const CL_StringRef &format, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7, Arg8 arg8, CL_DBCommand::Type type = CL_DBCommand::sql_statement)
{ return begin_arg(sql, format, type).set_arg(arg1).set_arg(arg2).set_arg(arg3).set_arg(arg4).set_arg(arg5).set_arg(arg6).set_arg(arg7).set_arg(arg8).get_result(); }

template <class Arg1, class Arg2, class Arg3, class Arg4, class Arg5, class Arg6, class Arg7, class Arg8, class Arg9>
CL_DBCommand create_sql_command(CL_DBConnection &sql, const CL_StringRef &format, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7, Arg8 arg8, Arg9 arg9, CL_DBCommand::Type type = CL_DBCommand::sql_statement)
{ return begin_arg(sql, format, type).set_arg(arg1).set_arg(arg2).set_arg(arg3).set_arg(arg4).set_arg(arg5).set_arg(arg6).set_arg(arg7).set_arg(arg8).set_arg(arg9).get_result(); }


struct SearchQuery
{
    CL_String name;
    int start;
    int limit;
};

struct GenreItem : CL_ListViewItemUserData
{
    int id;
    CL_String name;
};

struct StatusItem
{
    int id;
    CL_String name;
};

struct ShowItem : CL_ListViewItemUserData
{
    int id;

    CL_DateTime date_added;
    CL_DateTime date_updated;

    CL_String title;
    CL_String type;
    int year;
    int episodes;
    int season;
    std::vector<GenreItem> genres;
    double rating;
    CL_String comment;

    int status;
};


typedef std::map<CL_String, CL_String> HTTPHeaderFields;

class HTTPHeader
{
private:
    HTTPHeaderFields fields;

public:
    HTTPHeader()
    {
        fields["Connection"] = "close";
        fields["Accept"] = "text/plain, text/html";
        fields["User-Agent"] = "AnimeRecord/1.0";
    }

    CL_String &operator[](const CL_String &field)
    {
        return fields[field];
    }

    CL_String operator[](const CL_String &field) const
    {
        HTTPHeaderFields::const_iterator i = fields.find(field);
        if(i != fields.end())
            return i->second;
        else 
            return "";
    }

    HTTPHeaderFields::iterator begin()
    {
        return fields.begin();
    }
    HTTPHeaderFields::iterator end()
    {
        return fields.end();
    }

    HTTPHeaderFields::const_iterator begin() const
    {
        return fields.begin();
    }
    HTTPHeaderFields::const_iterator end() const
    {
        return fields.end();
    }
};

class HTTPClient
{

    CL_String host;
    CL_String port;
    CL_String auth_string;
    CL_TCPConnection connection;

private:
    CL_String header_to_string(const HTTPHeader &header) const
    {
        struct JoinHTTPFields
        {
            CL_String operator()(const CL_String &acc, const std::pair<CL_String, CL_String> &field) const
            {
                return cl_format("%1%2: %3\r\n", acc, field.first, field.second);
            }
        };
        return std::accumulate(header.begin(), header.end(), CL_String(), JoinHTTPFields()) + "\r\n";
    }

    CL_String encode_http_auth(const CL_String&user, const CL_String &pass) const
    {
        if(user.empty() && pass.empty())
        {
            return CL_String();
        }

        CL_String auth = CL_Base64Encoder::encode(cl_format("%1:%2", user, pass));
        return cl_format("Basic %1", auth);
    }

    CL_String url_encode(const CL_String &url)
    {
        static std::map<char, CL_String> encode_map;

        if(encode_map.empty())
        {
            encode_map[' '] = "%20";
            encode_map['<'] = "%3C";
            encode_map['>'] = "%3E"; 
            encode_map['%'] = "%25"; 
            encode_map['{'] = "%7B"; 
            encode_map['}'] = "%7D"; 
            encode_map['|'] = "%7C"; 
            encode_map['\\'] = "%5C"; 
            encode_map['^'] = "%5E"; 
            encode_map['~'] = "%7E";
            encode_map['['] = "%5B";
            encode_map[']'] = "%5D";
            encode_map['`'] = "%60";
            encode_map['\''] = "%27";
            encode_map[';'] = "%3B";
            encode_map[':'] = "%3A"; 
            encode_map['@'] = "%40"; 
            encode_map['$'] = "%24";
        }

        struct EncodeUrl
        {
            std::map<char, CL_String> &encode_map;

            EncodeUrl(std::map<char, CL_String> &encode_map) : encode_map(encode_map){}
            
            CL_String operator()(const CL_String &acc, char ch) const
            {
                std::map<char, CL_String>::const_iterator i = encode_map.find(ch);
                if(i == encode_map.end())
                {
                    return acc+CL_String(1, ch);
                }
                else
                {
                    return acc+i->second;
                }
            }
        };

        return std::accumulate(url.begin(), url.end(), CL_String(), EncodeUrl(encode_map));
    }

public:
    HTTPClient(const CL_String &host, const CL_String &port, const CL_String &username="", const CL_String &password="")
        : host(host), port(port), auth_string(encode_http_auth(username, password)), connection(CL_SocketName(host, port))
    {
        connection.set_nodelay(true);
    }

    ~HTTPClient()
    {
        close();
    }

    void close()
    {
        connection.disconnect_graceful();
    }

    CL_String get_header_string(const HTTPHeader &header) const
    {
        return header_to_string(header);
    }

    CL_String download_url(const CL_String &path, const HTTPHeader &header, const CL_String &refererer_url="", int timeout=15000)
    {
        CL_String request;

        CL_String encoded_path = url_encode(path);
        request = cl_format("GET %1 HTTP/1.1\r\n", encoded_path);

        HTTPHeader header_copy = header;

        if(auth_string.empty() == false)
        {
            header_copy["Authorization"] = auth_string;
        }

        header_copy["Host"] = host;

        if(refererer_url.empty() == false)
        {
            header_copy["Referer"] = refererer_url;
        }

        request += header_to_string(header_copy);

        connection.send(request.data(), request.length(), true);      

        CL_String response;
        while (connection.get_read_event().wait(timeout))
        {
            char buffer[16*1024];
            int received = connection.read(buffer, 16*1024, false);
            if (received == 0)
                break;
            response.append(buffer, received);
        }
        
        CL_String response_header = response.substr(0, response.find("\r\n\r\n"));
        CL_String content = response.substr(response_header.length() + 4);

        if (response_header.find("Transfer-Encoding: chunked") != CL_String::npos)
        {
            CL_String::size_type start = 0;
            while (true)
            {
                CL_String::size_type end = content.find("\r\n", start);
                if (end == CL_String::npos)
                    end = content.length();

                CL_String str_length = content.substr(start, end-start);
                int length = CL_StringHelp::text_to_int(str_length, 16);
                content = content.substr(0, start) + content.substr(end+2);
                start += length;


                end = content.find("\r\n", start);
                if (end == CL_String::npos)
                    end = content.length();
                content = content.substr(0, start) + content.substr(end+2);

                if (length == 0)
                    break;
            }
        }

        return content;
    }

};

class MyAnimeListClient
{
public:
    std::vector<CL_String> get_genres(int showid) const
    {
        // unofficial myanimelist API!
        // this function is very slow, only use it when required
        HTTPHeader header;
        HTTPClient client("mal-api.com", "80");

        CL_String xmldoc = client.download_url(cl_format("/anime/%1?format=xml", showid), header);

        CL_DataBuffer docdata(xmldoc.data(), xmldoc.length());
        CL_IODevice_Memory docmem(docdata);
        CL_DomDocument doc(docmem);

        CL_XPathEvaluator xpath;
        CL_XPathObject obj = xpath.evaluate("anime/genre", doc);
        std::vector<CL_DomNode> nodes = obj.get_node_set();

        std::vector<CL_String> genres;
        for(std::vector<CL_DomNode>::iterator it = nodes.begin(); it != nodes.end(); ++it)
        {
            genres.push_back(it->to_element().get_text());
        }

        return genres;
    }

    std::map<int,ShowItem> search(const CL_String &query) const
    {
        HTTPHeader header;
        HTTPClient client("myanimelist.net", "80", "animerecord", "animerecord");

        CL_String xmldoc = client.download_url(cl_format("/api/anime/search.xml?q=%1", query), header);


        if(xmldoc == "Invalid credentials")
            throw CL_Exception("Invalid username or password");

        if(xmldoc == "No results")
            xmldoc = "";


        CL_DataBuffer docdata(xmldoc.data(), xmldoc.length());
        CL_IODevice_Memory docmem(docdata);
        CL_DomDocument doc(docmem);
       
        CL_XPathEvaluator xpath;
        CL_XPathObject obj = xpath.evaluate("anime/entry", doc);
        std::vector<CL_DomNode> nodes = obj.get_node_set();

        std::map<int,ShowItem> shows;

        for(std::vector<CL_DomNode>::iterator it = nodes.begin(); it != nodes.end(); ++it)
        {
            ShowItem show;
            CL_DomNode child = it->get_first_child();
            CL_String output;
            int showid;
            while(child.is_null() == false)
            {
                if(child.get_node_name() == "id")
                    showid = CL_StringHelp::text_to_int(child.to_element().get_text());

                if(child.get_node_name() == "title")
                    show.title = child.to_element().get_text();

                if(child.get_node_name() == "score")
                    show.rating = CL_StringHelp::text_to_double(child.to_element().get_text());

                if(child.get_node_name() == "episodes")
                    show.episodes = CL_StringHelp::text_to_int(child.to_element().get_text());

                // example date: 2007-12-22
                if(child.get_node_name() == "start_date")
                {
                    show.year = CL_StringHelp::text_to_int(child.to_element().get_text().substr(0,4));
                }

                if(child.get_node_name() == "synopsis")
                    show.comment = child.to_element().get_text();


                child = child.get_next_sibling();
            }

            show.id = -1;
            show.type = "Anime";
            show.status = PLANNING;
            show.season = 1;

            shows[showid] = show;
        }

        return shows;
    }
};


class Database
{    
    CL_SharedPtr<CL_DBConnection> sql;
        
    template<typename StrType>
    StrType strip_sql_symbol(const StrType &s) const
    {
        return clean(s, StrType("%_"));
    }


    /// \brief Create database command with 8 input arguments.
    template <class Arg1, class Arg2, class Arg3, class Arg4, class Arg5, class Arg6, class Arg7, class Arg8>
    CL_DBCommand create_command(const CL_StringRef &format, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7, Arg8 arg8, CL_DBCommand::Type type = CL_DBCommand::sql_statement)
    { return begin_arg(format, type).set_arg(arg1).set_arg(arg2).set_arg(arg3).set_arg(arg4).set_arg(arg5).set_arg(arg6).set_arg(arg7).set_arg(arg8).get_result(); }

    /// \brief Create database command with 9 input arguments.
    template <class Arg1, class Arg2, class Arg3, class Arg4, class Arg5, class Arg6, class Arg7, class Arg8, class Arg9>
    CL_DBCommand create_command(const CL_StringRef &format, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7, Arg8 arg8, Arg9 arg9, CL_DBCommand::Type type = CL_DBCommand::sql_statement)
    { return begin_arg(format, type).set_arg(arg1).set_arg(arg2).set_arg(arg3).set_arg(arg4).set_arg(arg5).set_arg(arg6).set_arg(arg7).set_arg(arg8).set_arg(arg9).get_result(); }

    /// \brief Create database command with 10 input arguments.
    template <class Arg1, class Arg2, class Arg3, class Arg4, class Arg5, class Arg6, class Arg7, class Arg8, class Arg9, class Arg10>
    CL_DBCommand create_command(const CL_StringRef &format, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5, Arg6 arg6, Arg7 arg7, Arg8 arg8, Arg9 arg9, Arg10 arg10, CL_DBCommand::Type type = CL_DBCommand::sql_statement)
    { return begin_arg(format, type).set_arg(arg1).set_arg(arg2).set_arg(arg3).set_arg(arg4).set_arg(arg5).set_arg(arg6).set_arg(arg7).set_arg(arg8).set_arg(arg9).set_arg(arg10).get_result(); }


public:

    Database()
    {
        CL_String databaseFile = "animerecord.s3db";
        // test to see if the file exist or not by opening it
        CL_File file(databaseFile, CL_File::open_existing, CL_File::access_read_write);
        file.close();
        sql = CL_SharedPtr<CL_DBConnection>(new CL_SqliteConnection(databaseFile));
    }

    // return alphabetically sorted show genres
    std::vector<GenreItem> get_all_genres()
    {
        CL_DBCommand cmd = sql->create_command("select id, name from genre order by name collate nocase");
        CL_DBReader reader = sql->execute_reader(cmd);

        std::vector<GenreItem> genres;
        while(reader.retrieve_row())
        {
            GenreItem item;
            item.id = reader.get_column_value("id");
            item.name = reader.get_column_value("name");
            genres.push_back(item);
        }

        return genres;
    }

    std::vector<StatusItem> get_all_status()
    {
        CL_DBCommand cmd = sql->create_command("select id, name from status order by id asc");
        CL_DBReader reader = sql->execute_reader(cmd);

        std::vector<StatusItem> statuses;
        while(reader.retrieve_row())
        {
            StatusItem item;
            item.id = reader.get_column_value("id");
            item.name = reader.get_column_value("name");
            statuses.push_back(item);
        }

        return statuses;
    }

    // returns the GenreItems that were passed to this function with the ID set
    std::vector<GenreItem> get_genres_by_name(const std::vector<CL_String> &genreStrs)
    {
        std::vector<GenreItem> genres;
        for (std::vector<CL_String>::const_iterator it = genreStrs.begin(); it != genreStrs.end(); ++it)
        {
            CL_DBCommand cmd = sql->create_command("select id from genre "
                                                   "where name = ?1 ", *it);
            int id = sql->execute_scalar_int(cmd);

            GenreItem item;
            item.id = id;
            item.name = *it;
            genres.push_back(item);
        }

        return genres;
    }

    // ensures all genreStrs are added to the database
    void ensure_add_genres(const std::vector<CL_String> &genreStrs)
    {
        for (std::vector<CL_String>::const_iterator it = genreStrs.begin(); it != genreStrs.end(); ++it)
        {
            CL_DBTransaction trans = sql->begin_transaction();
            CL_DBCommand cmd = sql->create_command("insert into genre (name) "
                                                   "select ?1 " 
                                                   "where not exists (select * from genre where name = ?1 collate nocase)", *it);
            sql->execute_non_query(cmd);
            trans.commit();
        }
    }

    // returns the inserted show id
    int add_show(const CL_String &title, const CL_String &type, const std::vector<GenreItem> &genres, int year, int rating, const CL_String &comment, int episodes, int season, int status)
    {
        CL_String title_s = strip_sql_symbol(title);
        CL_String type_s = strip_sql_symbol(type);
        CL_String comment_s = strip_sql_symbol(comment);

        if(title_s.empty())
            throw CL_Exception("title is empty!");

        CL_DBTransaction transaction = sql->begin_transaction();

        CL_DBCommand cmd = create_sql_command(*sql, "insert into show (title, type, year, rating, comment, episodes, season, status) values (?1,?2,?3,?4,?5,?6,?7,?8)",
                                                title_s, type_s, year, rating, comment_s, episodes, season, status);
        sql->execute_non_query(cmd);

        int showid = cmd.get_output_last_insert_rowid();

        cmd = sql->create_command("insert into show_genre (show_id, genre_id) values (?1,?2)");
        cmd.set_input_parameter(1, showid);

        for (std::vector<GenreItem>::const_iterator it = genres.begin(); it != genres.end(); ++it)
        {
            cmd.set_input_parameter(2, it->id);
            sql->execute_non_query(cmd);
        }

        transaction.commit();

        return showid;
    }

    void update_show(int showid, const CL_String &title, const CL_String &type, const std::vector<GenreItem> &genres, 
                     int year, int rating, const CL_String &comment, int episodes, int season, int status)
    {
        CL_String title_s = strip_sql_symbol(title);
        CL_String type_s = strip_sql_symbol(type);
        CL_String comment_s = strip_sql_symbol(comment);

        if(title_s.empty())
            throw CL_Exception("title is empty!");

        CL_DBTransaction transaction = sql->begin_transaction();

        CL_DBCommand cmd = create_sql_command(*sql, "update show set title=?2, type=?3, year=?4, rating=?5, comment=?6, episodes=?7, season=?8, status=?9 where id=?1",
                                               showid, title_s, type_s, year, rating, comment_s, episodes, season, status);
        sql->execute_non_query(cmd);

        cmd = sql->create_command("delete from show_genre where show_id=?1", showid);
        sql->execute_non_query(cmd);

        cmd = sql->create_command("insert into show_genre (show_id, genre_id) values (?1,?2)");
        cmd.set_input_parameter(1, showid);

        for (std::vector<GenreItem>::const_iterator it = genres.begin(); it != genres.end(); ++it)
        {
            cmd.set_input_parameter(2, it->id);
            sql->execute_non_query(cmd);
        }

        transaction.commit();
    }

    bool has_row(CL_DBCommand &cmd)
    {
        CL_DBReader reader = sql->execute_reader(cmd);
        return reader.retrieve_row();
    }

    bool show_exist(const CL_String &title, const CL_String &type, int year, int season)
    {
        CL_DBCommand cmd = sql->create_command("select id from show where title like ?1 and type=?2 and year=?3 and season=?4", title, type, year, season);
        return has_row(cmd);        
    }

    bool show_exist(int showid)
    {
        CL_DBCommand cmd = sql->create_command("select id from show where id=?1", showid);
        return has_row(cmd);        
    }

    // find out if the current show matches another show in the database or not
    bool show_similar_to(int showid, const CL_String &title, const CL_String &type, int year, int season)
    {
        CL_DBCommand cmd = sql->create_command("select id from show where id<>?1 and title=?2 and type=?3 and year=?4 and season=?5", 
                                                showid, title, type, year, season);
        return has_row(cmd);        
    }

    std::vector<GenreItem> find_show_genres(int id)
    {
        CL_DBCommand genreCmd = sql->create_command("select show_genre.genre_id, genre.name "
                                                    "from show, show_genre, genre "
                                                    "where show.id = show_genre.show_id and show_genre.genre_id = genre.id and show.id = ?1", id);

        CL_DBReader genreReader = sql->execute_reader(genreCmd);

        std::vector<GenreItem> genres;

        while(genreReader.retrieve_row())
        {
            GenreItem gi;
            gi.id = genreReader.get_column_value("genre_id");
            gi.name = genreReader.get_column_value("name");
            genres.push_back(gi);
        }

        return genres;
    }

    ShowItem read_show(CL_DBReader &reader)
    {
        ShowItem show;

        show.id = reader.get_column_value("id");
        show.date_added = reader.get_column_value("date_added");
        show.date_updated = reader.get_column_value("date_updated");
        show.title = reader.get_column_value("title");
        show.type = reader.get_column_value("type");
        show.year = reader.get_column_value("year");
        show.episodes = reader.get_column_value("episodes");
        show.season = reader.get_column_value("season");
        show.rating = reader.get_column_value("rating");
        show.comment = reader.get_column_value("comment");
        show.status = reader.get_column_value("status");

        return show;
    }

    ShowItem find_show(int id)
    {
        ShowItem show;

        CL_DBCommand cmd = sql->create_command("select id, date_added, date_updated, title, type, year, episodes, season, rating, comment, status " 
                                               "from show where id = ?1", id);
        CL_DBReader reader = sql->execute_reader(cmd);

        if(reader.retrieve_row())
        {
            show = read_show(reader);
            reader.close();

            show.genres = find_show_genres(show.id);     
        }

        return show;
    }

    // TODO full text search
    std::vector<ShowItem> find_shows(const CL_String &title, int statusmask, 
                                     int start, int limit)
    {
        std::vector<ShowItem> shows;

        CL_DBCommand cmd;

        std::vector<CL_String> where_preds;

        CL_String status_line;

        if(statusmask > 0)
        {
            status_line = " status in (";

            int i = 1;
            std::vector<CL_String> statuses;
            while(statusmask > 0)
            {
                int s = (statusmask >> i) & 1;

                if(s)
                {
                    statuses.push_back(CL_StringHelp::int_to_text(i));
                }
                statusmask &= ~(1 << i);
                i++;
            }
            status_line += join(statuses.begin(), statuses.end(), CL_String(",")) + ") ";
            where_preds.push_back(status_line);
        }

        if(start == -1 && limit == -1)
        {
            cmd = sql->create_command(CL_String("select id, date_added, date_updated, title, type, year, episodes, season, rating, comment, status from show ") +
                                      (status_line.empty() ? CL_String() : CL_String(" where ") + status_line) +
                                      CL_String("order by title COLLATE NOCASE ") );
        }
        else
        {
            cmd = sql->create_command(CL_String("select id, date_added, date_updated, title, type, year, episodes, season, rating, comment, status from show  ") +
                                      CL_String("where title like ?1 ") + (status_line.empty() ? CL_String() : CL_String(" and ") + status_line) +
                                      CL_String("order by title COLLATE NOCASE " 
                                                "limit ?2, ?3 "),
                                                title.empty() ? "%" : title, start, limit);
        }

    
        CL_DBReader reader = sql->execute_reader(cmd);

        while(reader.retrieve_row())
        {
            shows.push_back(read_show(reader));           
        }
        reader.close();

        for (std::vector<ShowItem>::iterator it = shows.begin(); it != shows.end(); ++it)
        {
            it->genres = find_show_genres(it->id);
        }

        return shows;
    }

    std::vector<ShowItem> find_all_shows()
    {
        return find_shows("", ALL_VIEWING_STATUS_MASK, -1, -1);
    }
};


class Page
{

    int id;

public:
    Page(int id) : id(id)
    {

    }
    virtual ~Page(){}

    int get_id() const
    {
        return id;
    }
    virtual void fill_page(const void *data)
    {

    }
};


class TabManager
{
    CL_Tab *tab;

    // pages
    std::auto_ptr<Page> addPage;
    std::auto_ptr<Page> viewPage;
    std::auto_ptr<Page> searchPage;

public:
    TabManager(CL_GUIComponent *parent, const CL_SharedPtr<Database> &database);
    ~TabManager(){}

    CL_Tab *get_tab() const;

    void load_show_item(const ShowItem &si)
    {
        addPage->fill_page(&si);
    }

    void display_show_item(const ShowItem &si)
    {
        load_show_item(si);
        get_tab()->show_page(addPage->get_id());
    }
};

template<typename T1, typename T2>
class UserItemPair : public std::pair<T1,T2>, public CL_ListViewItemUserData
{
public:
    UserItemPair &operator =(const std::pair<T1,T2> &rhs)
    {
        std::pair<T1,T2>::operator =(rhs);
        return *this;
    }
};

typedef UserItemPair<int,ShowItem> ShowItemPair;

class SearchPage : public Page
{
    CL_TabPage *page;
    TabManager *tabMan;
    CL_SharedPtr<Database> database;

    CL_ListView *result;
    CL_LineEdit *search;
    CL_PushButton *previous, *next, *edit;
    CL_LineEdit *pagenumber;

    int currentPage;

    enum { LIMIT = 100 };

    void on_search_enter_pressed()
    {
        MyAnimeListClient client;
        std::map<int, ShowItem> shows = client.search(search->get_text());

        CL_ListViewItem docItem = result->get_document_item();

        while(docItem.get_child_count() != LIMIT)
        {
            CL_ListViewItem item = result->create_item();
            docItem.append_child(item);
        }

        CL_ListViewItem child = docItem.get_first_child();

        CL_ListViewColumnHeader titleColumn = result->get_header()->get_column("title");
        CL_ListViewColumnHeader ratingColumn = result->get_header()->get_column("rating");
        CL_ListViewColumnHeader commentColumn = result->get_header()->get_column("comment");

        CL_String titleColumnId = titleColumn.get_column_id();
        CL_String ratingColumnId = ratingColumn.get_column_id();
        CL_String commentColumnId = commentColumn.get_column_id();

        CL_String titleColumnName = cl_format("Title(%1)", shows.size());

        CL_GUIThemePart listThemePart(result, "selection");
        CL_Font font = listThemePart.get_font();
        int padding = listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-right", "4")) + 
                      listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-left", "3")) + 5;

        int maxWidth = font.get_text_size(result->get_gc(), titleColumnName).width + padding;

        std::vector<std::pair<int,ShowItem> > showsVector;
        for (std::map<int, ShowItem>::const_iterator it = shows.begin(); it != shows.end(); ++it)
        {            
            showsVector.push_back(*it);
        }

        struct SortFun
        {
            bool operator()(const std::pair<int,ShowItem> &show1, const std::pair<int,ShowItem> &show2)
            {
                return show1.second.title < show2.second.title;
            }
        };

        std::sort(showsVector.begin(), showsVector.end(), SortFun());

        for (std::vector<std::pair<int,ShowItem> >::const_iterator it = showsVector.begin(); it != showsVector.end(); ++it)
        {  
            int textWidth = font.get_text_size(result->get_gc(), it->second.title).width + padding;
            if(maxWidth < textWidth) maxWidth = textWidth;

            ShowItemPair *showItemCopy = new ShowItemPair;
            *showItemCopy = *it;

            child.set_userdata(CL_SharedPtr<ShowItemPair>(showItemCopy));
            child.set_column_text(titleColumnId, it->second.title);
            child.set_column_text(ratingColumnId, CL_StringHelp::double_to_text(it->second.rating, 2));
            child.set_column_text(commentColumnId, clean(it->second.comment, CL_String("\r\n")));
            child = child.get_next_sibling();
        }

        if(maxWidth > 0) titleColumn.set_width(maxWidth);

        int ellipseWidth = font.get_text_size(result->get_gc(), "...  ").width + padding;
        commentColumn.set_width(result->get_width() - titleColumn.get_width()-ratingColumn.get_width()-ellipseWidth);

        while(child.is_null() == false)
        {
            child.set_userdata(CL_SharedPtr<ShowItem>());
            child.set_column_text(titleColumnId, "");
            child.set_column_text(ratingColumnId, "");
            child.set_column_text(commentColumnId, "");
            child = child.get_next_sibling();
        }

        titleColumn.set_caption(titleColumnName);
        //update_current_page_number();

    }

    void on_edit_clicked()
    {
        if(result->get_selected_item().is_null() == false)
        {
            CL_SharedPtr<std::pair<int,ShowItem> > show = cl_dynamic_pointer_cast<std::pair<int,ShowItem> >(result->get_selected_item().get_userdata());
            if(show)
            {
                if(show->second.genres.empty())
                {
                    MyAnimeListClient client;

                    std::vector<CL_String> genreStrs = client.get_genres(show->first);
                    std::vector<GenreItem> genreItems = database->get_all_genres();


                    struct CompareFun
                    {
                        bool operator()(const CL_String &i1, const GenreItem &i2)
                        {
                            return i1 < i2.name;
                        }
                        bool operator()(const GenreItem &i1, const CL_String &i2)
                        {
                            return i1.name < i2;
                        }
                        bool operator()(const GenreItem &i1, const GenreItem &i2)
                        {
                            return i1.name < i2.name;
                        }
                    };


                    std::sort(genreStrs.begin(), genreStrs.end(), std::less<CL_String>());
                    std::sort(genreItems.begin(), genreItems.end(), CompareFun());

                    std::vector<CL_String> genreStrsOrig = genreStrs;

                    std::vector<CL_String>::const_iterator genreStrsEnd = std::set_difference(genreStrs.begin(), genreStrs.end(), 
                                                                                              genreItems.begin(), genreItems.end(), 
                                                                                              genreStrs.begin(), CompareFun());
                    std::vector<CL_String>::const_iterator genreStrsStart = genreStrs.begin();
                    genreStrs.resize(std::distance(genreStrsStart, genreStrsEnd));

                    if(genreStrs.empty() == false)
                    {
                        MessageDialog msg(page, "Question", 
                                          cl_format("The following genres will be added to the database:\n%1\nDo you wish to continue?", 
                                                    join(genreStrs.begin(), genreStrs.end(), CL_String(", "))),
                                          MessageDialog::ASK_YES_NO);
                        msg.exec();

                        if(msg.getResult() == MessageDialog::YES)
                        {
                            database->ensure_add_genres(genreStrs);
                        }
                        else
                        {
                            return;
                        }
                    }
                    show->second.genres = database->get_genres_by_name(genreStrsOrig);
                }
                                
                tabMan->display_show_item(show->second);
            }   
        }
    }

public:
    SearchPage(CL_TabPage *page, TabManager *tabMan, const CL_SharedPtr<Database> &database) 
        : Page(page->get_id()), tabMan(tabMan), page(page), database(database), currentPage(0),
        pagenumber(CL_LineEdit::get_named_item(page, "pagenumber")),
        result(CL_ListView::get_named_item(page, "result")),
        search(CL_LineEdit::get_named_item(page, "search")),
        previous(CL_PushButton::get_named_item(page, "previous")),
        next(CL_PushButton::get_named_item(page, "next")),
        edit(CL_PushButton::get_named_item(page, "edit"))
    {
        result->show_detail_icon(false);
        result->show_detail_opener(false);

        search->func_enter_pressed().set(this, &SearchPage::on_search_enter_pressed);

        //previous->func_clicked().set(this, &SearchPage::on_previous_clicked);
        //next->func_clicked().set(this, &SearchPage::on_next_clicked);
        edit->func_clicked().set(this, &SearchPage::on_edit_clicked);

        result->get_icon_list().clear();
        result->set_multi_select(false);
        result->set_select_whole_row(true);

        pagenumber->set_alignment(CL_LineEdit::align_center);
        pagenumber->set_read_only(true);

        CL_ListViewColumnHeader column = result->get_header()->create_column("title", "Title");
        result->get_header()->append(column);


        CL_GUIThemePart listThemePart(result, "selection");
        int padding = listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-right", "4")) + 
            listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-left", "3")) + 5;

        column = result->get_header()->create_column("rating", "Rating");
        result->get_header()->append(column);
        column.set_width(listThemePart.get_font().get_text_size(result->get_gc(), "Rating").width+padding);

        column = result->get_header()->create_column("comment", "Comment");
        result->get_header()->append(column);
    }

    virtual ~SearchPage()
    {

    }
};

class AddPage : public Page
{
    CL_TabPage *page;
    CL_SharedPtr<Database> database;

    CL_PopupMenu genrePopMenu;
    CL_PopupMenu statusPopMenu;
    CL_PopupMenu pop; //generic popup menu, currently used for display search results

public:
    AddPage(CL_TabPage *page, TabManager *tabMan, const CL_SharedPtr<Database> &database) : Page(page->get_id()), page(page), database(database)
    {
        CL_LineEdit::get_named_item(page, "title");
        CL_Spin &year = *CL_Spin::get_named_item(page, "year");
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_Label &ratingLabel = *CL_Label::get_named_item(page, "ratingLabel");
        CL_TextEdit &comment = *CL_TextEdit::get_named_item(page, "comment");
        CL_PushButton &addButton = *CL_PushButton::get_named_item(page, "add");
        CL_PushButton &clearButton = *CL_PushButton::get_named_item(page, "clear");
        CL_PushButton &searchButton = *CL_PushButton::get_named_item(page, "search");
        CL_PushButton &updateButton = *CL_PushButton::get_named_item(page, "update");
        CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
        CL_Spin &season = *CL_Spin::get_named_item(page, "season");

        CL_ComboBox &mediatype = *CL_ComboBox::get_named_item(page, "mediatype");
        CL_ComboBox &status = *CL_ComboBox::get_named_item(page, "status");

        CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");
        CL_LineEdit &date_added = *CL_LineEdit::get_named_item(page, "date_added");
        CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");

        CL_ListView &genreSelection = *CL_ListView::get_named_item(page, "genreSelection");
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");
        CL_PushButton &addGenre = *CL_PushButton::get_named_item(page, "addGenre");
        CL_PushButton &removeGenre = *CL_PushButton::get_named_item(page, "removeGenre");

        genreSelection.show_detail_icon(false);
        genreSelection.show_detail_opener(false);
        genreAdded.show_detail_icon(false);
        genreAdded.show_detail_opener(false);

        genrePopMenu.insert_item("Anime");
        genrePopMenu.insert_item("Film");
        genrePopMenu.insert_item("TV");
        mediatype.set_popup_menu(genrePopMenu);
        mediatype.set_selected_item(0);
        mediatype.set_editable(false);
        mediatype.set_focus_policy(CL_GUIComponent::focus_refuse);
                       
        id.set_read_only(true);
        date_added.set_read_only(true);
        date_updated.set_read_only(true);

        year.set_floating_point_mode(false);
        year.set_ranges(1800,3000);
        year.set_value(2010);
        year.set_step_size(1);

        episodes.set_floating_point_mode(false);
        episodes.set_ranges(0, 0x7fffffff);
        episodes.set_value(0);
        episodes.set_step_size(1);

        season.set_floating_point_mode(false);
        season.set_ranges(0, 0x7fffffff);
        season.set_value(0);
        season.set_step_size(1);

        ratingLabel.set_text("5/10");
        rating.set_position(5);
        rating.func_value_changed().set(this, &AddPage::on_rating_changed);

        addButton.func_clicked().set(this, &AddPage::on_submit_clicked);
        clearButton.func_clicked().set(this, &AddPage::on_clear_clicked);
        searchButton.func_clicked().set(this, &AddPage::on_search_clicked);
        updateButton.func_clicked().set(this, &AddPage::on_update_clicked);

        addGenre.func_clicked().set(this, &AddPage::on_add_genre_clicked);
        removeGenre.func_clicked().set(this, &AddPage::on_remove_genre_clicked);

        refresh_available_genre_list();

        CL_ListViewColumnHeader header = genreAdded.get_header()->create_column("genreAdded", "Selected Genre(0)");
        header.set_width(genreAdded.get_width());
        genreAdded.get_header()->append(header);

        genreSelection.set_multi_select(false);
        genreAdded.set_multi_select(false);

        std::vector<StatusItem> statuses = this->database->get_all_status();

        for(std::vector<StatusItem>::const_iterator it = statuses.begin(); it != statuses.end(); ++it)
        {
            statusPopMenu.insert_item(it->name, it->id);
        }

        status.set_popup_menu(statusPopMenu);
        status.set_editable(false);
        status.set_selected_item(0);
        status.set_focus_policy(CL_GUIComponent::focus_refuse);
    }


    virtual ~AddPage() {}

    virtual void fill_page(const void *data)
    {
        const ShowItem *si = static_cast<const ShowItem*>(data);
        fill_page(*si);
    }

private:

    void update_genre_added_header() const
    {
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");
        genreAdded.get_header()->get_column("genreAdded").set_caption(cl_format("Selected Genre(%1)", genreAdded.get_document_item().get_child_count()));
    }

    std::vector<CL_SharedPtr<GenreItem> > refresh_available_genre_list() 
    {
        CL_ListView &genreSelection = *CL_ListView::get_named_item(page, "genreSelection");

        genreSelection.clear();

        std::vector<GenreItem> genreItems = this->database->get_all_genres();

        std::vector<CL_SharedPtr<GenreItem> > genreItemsPtr;

        if(genreSelection.get_header()->get_column("genreSelection").is_null())
        {
            CL_ListViewColumnHeader header = genreSelection.get_header()->create_column("genreSelection", "Available Genre");
            header.set_width(genreSelection.get_width());
            genreSelection.get_header()->append(header);
        }

        CL_ListViewItem docItem = genreSelection.get_document_item();
        for(std::vector<GenreItem>::const_iterator it = genreItems.begin(); it != genreItems.end(); ++it)
        {
            CL_ListViewItem item = genreSelection.create_item();
            CL_SharedPtr<GenreItem> newGenreItem = CL_SharedPtr<GenreItem>(new GenreItem);
            genreItemsPtr.push_back(newGenreItem);
            *newGenreItem = *it;
            item.set_userdata(newGenreItem);
            item.set_column_text("genreSelection", it->name);
            docItem.append_child(item);
        }
        genreSelection.get_header()->get_column("genreSelection").set_caption(cl_format("Available Genre(%1)", genreItems.size()));

        return genreItemsPtr;
    }

    void on_add_genre_clicked()
    {
        CL_ListView &genreSelection = *CL_ListView::get_named_item(page, "genreSelection");
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");

        if(genreSelection.get_selected_item().is_null() == false)
        {
            CL_String genreText = genreSelection.get_selected_item().get_column("genreSelection").get_text();
            CL_ListViewItem genreItem = genreAdded.find("genreAdded", genreText);
            if(genreItem.is_null())
            {
                CL_ListViewItem newGenreItem = genreAdded.create_item();
                newGenreItem.set_column_text("genreAdded", genreText);
                newGenreItem.set_userdata(genreSelection.get_selected_item().get_userdata());
                genreAdded.get_document_item().append_child(newGenreItem);

                // sort the added genre items by re-adding the items
                std::vector<CL_SharedPtr<GenreItem> > items;
                CL_ListViewItem child = genreAdded.get_document_item().get_first_child();
                while(child.is_null() == false)
                {
                    CL_SharedPtr<GenreItem> item = cl_dynamic_pointer_cast<GenreItem>(child.get_userdata());
                    items.push_back(item);
                    child = child.get_next_sibling();
                }
                genreAdded.clear();
                
                struct SortFun
                {
                    bool operator()(const CL_SharedPtr<GenreItem> &item1, const CL_SharedPtr<GenreItem> &item2)
                    {
                        return item1->name < item2->name;
                    }
                };

                std::sort(items.begin(), items.end(), SortFun());

                CL_ListViewItem genreAddedDocItem = genreAdded.get_document_item(); 
                for (std::vector<CL_SharedPtr<GenreItem> >::iterator it = items.begin(); it != items.end(); ++it)
                {
                    CL_ListViewItem newItem = genreAdded.create_item();
                    newItem.set_column_text("genreAdded", (*it)->name);
                    newItem.set_userdata(*it);
                    genreAddedDocItem.append_child(newItem);
                }
                update_genre_added_header();
            }
        }

    }

    void on_remove_genre_clicked()
    {
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");

        CL_ListViewItem selectedItem = genreAdded.get_selected_item();

        if(selectedItem.is_null() == false)
        {
            selectedItem.remove();
            
            update_genre_added_header();
        }
    }

    std::vector<GenreItem> get_selected_genres() const
    {
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");

        std::vector<GenreItem> genres;

        CL_ListViewItem child = genreAdded.get_document_item().get_first_child();
        while(child.is_null() == false)
        {
            CL_SharedPtr<GenreItem> item = cl_dynamic_pointer_cast<GenreItem>(child.get_userdata());
            genres.push_back(*item);
            child = child.get_next_sibling();
        }

        return genres;
    }

    void clear_genre() const
    {
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");
        genreAdded.clear();
        update_genre_added_header();
    }
    
    void on_rating_changed()
    {
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_Label &ratingLabel = *CL_Label::get_named_item(page, "ratingLabel");

        ratingLabel.set_text(cl_format("%1/10", rating.get_position()));
    }

    CL_String get_media_type() const
    {
        CL_ComboBox &mediatype = *CL_ComboBox::get_named_item(page, "mediatype");

        return mediatype.get_text();
    }

    int get_status() const
    {
        CL_ComboBox &status = *CL_ComboBox::get_named_item(page, "status");

        return status.get_selected_item();
    }

    void set_media_type(const CL_String &type) const
    {
        CL_ComboBox &mediatype = *CL_ComboBox::get_named_item(page, "mediatype");

        if(type == "Anime")
        {
            mediatype.set_text(type);
        }
        else if(type == "Film")
        {
            mediatype.set_text(type);
        }
        else if(type == "TV")
        {
            mediatype.set_text(type);
        }
        else reset_media_type();
    }

    void reset_media_type() const
    {
        CL_ComboBox &mediatype = *CL_ComboBox::get_named_item(page, "mediatype");
        mediatype.set_selected_item(0);
    }

    void reset_status() const
    {
        CL_ComboBox &status = *CL_ComboBox::get_named_item(page, "status");
        status.set_selected_item(0);
    }

    void set_status(int statusCode)
    {
        CL_ComboBox &status = *CL_ComboBox::get_named_item(page, "status");
        CL_PopupMenuItem item = statusPopMenu.get_item(statusCode);

        if(item.is_null() == false)
            status.set_selected_item(statusCode);
        else
            status.set_selected_item(0);
    }

    void fill_page(const ShowItem &show)
    {
        CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");
        CL_LineEdit &date_added = *CL_LineEdit::get_named_item(page, "date_added");
        CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");
        CL_LineEdit &title = *CL_LineEdit::get_named_item(page, "title");
        CL_Spin &year = *CL_Spin::get_named_item(page, "year");
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_TextEdit &comment = *CL_TextEdit::get_named_item(page, "comment");
        CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
        CL_Spin &season = *CL_Spin::get_named_item(page, "season");
        CL_ListView &genreAdded = *CL_ListView::get_named_item(page, "genreAdded");
        CL_ListView &genreSelection = *CL_ListView::get_named_item(page, "genreSelection");

        if(show.id != -1)
        {
            id.set_text(cl_format("%1", show.id));
        }
        else
        {
            id.set_text("");
        }
        id.request_repaint();

        if(show.date_added.is_null() == false)
        {
            date_added.set_text(show.date_added.to_local().to_short_datetime_string());
            
        }
        else
        {
            date_added.set_text("");
        }
        date_added.request_repaint();

        if(show.date_updated.is_null() == false)
        {
            date_updated.set_text(show.date_updated.to_local().to_short_datetime_string());
        }
        else
        {
            date_updated.set_text("");
        }
        date_updated.request_repaint();

        title.set_text(show.title);
        year.set_value(show.year);
        rating.set_position((int)show.rating);
        if(rating.func_value_changed().is_null() == false)
            rating.func_value_changed().invoke();
        comment.set_text(show.comment);
        episodes.set_value(show.episodes);
        season.set_value(show.season);
        set_status(show.status);
        set_media_type(show.type);
        clear_genre();

        // update the available genre list since it may have been changed when a new anime was added
        std::vector<CL_SharedPtr<GenreItem> > genreItems = refresh_available_genre_list();

        for(std::vector<GenreItem>::const_iterator it2 = show.genres.begin(); it2 != show.genres.end(); ++it2)
        {
            for(std::vector<CL_SharedPtr<GenreItem> >::const_iterator it = genreItems.begin(); it != genreItems.end(); ++it)
            {
                if(it2->id == (*it)->id)
                {
                    CL_ListViewItem newItem = genreAdded.create_item();
                    newItem.set_userdata(*it);
                    newItem.set_column_text("genreAdded", (*it)->name);
                    genreAdded.get_document_item().append_child(newItem);
                }
            }
        }

        update_genre_added_header();
    }


    void on_submit_clicked()
    {
        if(database)
        {
            CL_LineEdit &date_added = *CL_LineEdit::get_named_item(page, "date_added");
            CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");
            CL_LineEdit &title = *CL_LineEdit::get_named_item(page, "title");
            CL_Spin &year = *CL_Spin::get_named_item(page, "year");
            CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
            CL_TextEdit &comment = *CL_TextEdit::get_named_item(page, "comment");
            CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
            CL_Spin &season = *CL_Spin::get_named_item(page, "season");

            CL_String trimmedTitle = clean(CL_String(trimmed(title.get_text())), CL_String("%_"));

            if(trimmedTitle.empty())
            {
                MessageDialog(page, "Error", "The Title cannot be blank").exec();
            }
            else if(season.get_value() <= 0)
            {
                MessageDialog(page, "Error", "The Season number must be greater than 0").exec();
            }
            else if(get_selected_genres().empty())
            {
                MessageDialog(page, "Error", "You must select at least one genre").exec();
            }
            else
            {
                MessageDialog question(page, "Question", "Is everything correct?", MessageDialog::ASK_YES_NO);
                question.exec();
                question.set_visible(false);
                if(question.getResult() == MessageDialog::YES)
                {
                    if(database->show_exist(trimmedTitle, get_media_type(), year.get_value(), season.get_value()))
                    {
                        MessageDialog(page, "Error", "This show already exists in the database!\nClick Update to update it").exec();
                    }
                    else
                    {
                        CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");

                        int showid = database->add_show(trimmedTitle, get_media_type(), get_selected_genres(), year.get_value(), 
                                                        rating.get_position(), comment.get_text(), episodes.get_value(), season.get_value(), get_status());
                        ShowItem show = database->find_show(showid);
                        title.set_text(show.title);
                        id.set_text(cl_format("%1", show.id));
                        id.request_repaint();
                        date_added.set_text(show.date_added.to_local().to_short_datetime_string());
                        date_added.request_repaint();
                        date_updated.set_text(show.date_updated.to_local().to_short_datetime_string());
                        date_updated.request_repaint();
                        MessageDialog(page, "Done", "It's added!").exec();
                    }
                }

            }

        }
    }

    void on_update_clicked()
    {
        if(database)
        {
            CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");
            CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");
            CL_LineEdit &title = *CL_LineEdit::get_named_item(page, "title");
            CL_Spin &year = *CL_Spin::get_named_item(page, "year");
            CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
            CL_TextEdit &comment = *CL_TextEdit::get_named_item(page, "comment");
            CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
            CL_Spin &season = *CL_Spin::get_named_item(page, "season");
            
            CL_String trimmedTitle = clean(CL_String(trimmed(title.get_text())), CL_String("%_"));

            if(id.get_text().empty())
            {
                MessageDialog(page, "Error", "You haven't loaded a record yet!").exec();
            }
            else if(trimmedTitle.empty())
            {
                MessageDialog(page, "Error", "The Title cannot be blank").exec();
            }
            else if(season.get_value() <= 0)
            {
                MessageDialog(page, "Error", "The Season number must be greater than 0").exec();
            }
            else if(get_selected_genres().empty())
            {
                MessageDialog(page, "Error", "You must select at least one genre").exec();
            }
            else
            {
                MessageDialog question(page, "Question", "Is everything correct?", MessageDialog::ASK_YES_NO);
                question.exec();
                question.set_visible(false);
                if(question.getResult() == MessageDialog::YES)
                {                    
                    if(database->show_exist(id.get_text_int()) == false)
                    {
                        MessageDialog(page, "Error", "This show doesn't exists in the database!\nClick Add to add it").exec();
                    }
                    else if(database->show_similar_to(id.get_text_int(), trimmedTitle, get_media_type(), year.get_value(), season.get_value()))
                    {
                        MessageDialog(page, "Error", "A similar show with the same title, type, year and season already exist\nTry changing the title").exec();
                    }
                    else
                    {
                        database->update_show(id.get_text_int(), trimmedTitle, get_media_type(), get_selected_genres(), year.get_value(), 
                                              rating.get_position(), comment.get_text(), episodes.get_value(), season.get_value(), get_status());
                        ShowItem show = database->find_show(id.get_text_int());
                        title.set_text(show.title);
                        id.set_text(cl_format("%1", show.id));
                        id.request_repaint();
                        date_updated.set_text(show.date_updated.to_local().to_short_datetime_string());
                        date_updated.request_repaint();
                        MessageDialog(page, "Done", "It's updated!").exec();
                    }
                }

            }

        }
    }


    void on_clear_clicked()
    {
        CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");
        CL_LineEdit &date_added = *CL_LineEdit::get_named_item(page, "date_added");
        CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");
        CL_LineEdit &title = *CL_LineEdit::get_named_item(page, "title");
        CL_Spin &year = *CL_Spin::get_named_item(page, "year");
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_TextEdit &comment = *CL_TextEdit::get_named_item(page, "comment");
        CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
        CL_Spin &season = *CL_Spin::get_named_item(page, "season");

        id.set_text("");
        id.request_repaint();
        date_added.set_text("");
        date_added.request_repaint();
        date_updated.set_text("");
        date_updated.request_repaint();
        title.set_text("");
        year.set_value(2010);

        rating.set_position(5);
        if(rating.func_value_changed().is_null() == false)
            rating.func_value_changed().invoke();

        comment.set_text("");
        episodes.set_value(0);
        season.set_value(0);

        reset_status();
        reset_media_type();
        clear_genre();
    }

    void on_result_menu_click(ShowItem show)
    {
        fill_page(show);
    }

    void on_next_menu_click(SearchQuery search)
    {
        CL_PushButton &searchButton = *CL_PushButton::get_named_item(page, "search");

        CL_Rect geom = searchButton.get_geometry();
        display_search_result(search.name, search.start, search.limit, CL_Point(geom.left, geom.top));
    }

    void display_search_result(const CL_String &title, int start, int limit, const CL_Point &location)
    {
        CL_String trimmedTitle = trimmed(title);
        std::vector<ShowItem> shows = database->find_shows(trimmedTitle, ALL_VIEWING_STATUS_MASK, start, limit);

        pop.clear();
        pop.insert_item("Cancel");
        pop.insert_item("Clear").func_clicked().set(this, &AddPage::on_clear_clicked);
        pop.insert_separator();
        for (std::vector<ShowItem>::iterator it = shows.begin(); it != shows.end(); ++it)
        {
            pop.insert_item(cl_format("%1 (%2) (season %3) (%4)", it->title, it->year, it->season, it->type), it->id)
                .func_clicked().set(this, &AddPage::on_result_menu_click, *it);
        }

        
        if(shows.empty() == false)
            pop.insert_separator();

        SearchQuery search; 
        search.name = trimmedTitle; 

        if(start > 0)
        {
            search.start = cl_max(start-limit, 0); 
            search.limit = limit;
            pop.insert_item(cl_format("...Previous %1", limit)).func_clicked().set(this, &AddPage::on_next_menu_click, search);
        }

        if(shows.empty() == false)
        {
            search.start = start+limit; 
            search.limit = limit;
            pop.insert_item(cl_format("Next %1...", limit)).func_clicked().set(this, &AddPage::on_next_menu_click, search);
        }

        pop.start(page, page->component_to_screen_coords(location));
    }

    void on_search_clicked()
    {
        CL_LineEdit &title = *CL_LineEdit::get_named_item(page, "title");
        CL_PushButton &searchButton = *CL_PushButton::get_named_item(page, "search");

        CL_Rect geom = searchButton.get_geometry();
        display_search_result(title.get_text(), 0, 20, CL_Point(geom.left, geom.top));
    }

};

class ViewPage : public Page
{
    std::vector<ShowItem> shows;
    CL_SharedPtr<Database> database;
    TabManager *tabMan;

    CL_ListView *result;
    CL_LineEdit *search;
    CL_PushButton *previous, *next, *edit;
    CL_LineEdit *pagenumber;
    CL_CheckBox *watching, *completed, *planning, *dropped;

    enum { LIMIT=100 };
    unsigned int currentPage;
    CL_String query;

    int viewing_status_mask;

    void on_search_edit(CL_InputEvent &ev)
    {
        refresh_list();
    }

    void refresh_list() 
    {
        currentPage = 0;

        CL_ListViewItem docItem = result->get_document_item();
        query = cl_format("%%%1%%", CL_StringHelp::text_to_lower(search->get_text()));
        find_shows();
        populate_show_list();
    }
    void update_current_page_number()
    {
        pagenumber->set_text(cl_format("%1", currentPage+1));
    }

    std::vector<ShowItem>::size_type find_shows()
    {
        shows = database->find_shows(query, viewing_status_mask, currentPage*LIMIT, LIMIT);

        return shows.size();
    }

    void populate_show_list()
    {        
        CL_ListViewItem docItem = result->get_document_item();

        while(docItem.get_child_count() != LIMIT)
        {
            CL_ListViewItem item = result->create_item();
            docItem.append_child(item);
        }

        CL_ListViewItem child = docItem.get_first_child();

        CL_ListViewColumnHeader titleColumn = result->get_header()->get_column("title");
        CL_ListViewColumnHeader ratingColumn = result->get_header()->get_column("rating");
        CL_ListViewColumnHeader commentColumn = result->get_header()->get_column("comment");

        CL_String titleColumnId = titleColumn.get_column_id();
        CL_String ratingColumnId = ratingColumn.get_column_id();
        CL_String commentColumnId = commentColumn.get_column_id();
        
        CL_String titleColumnName = cl_format("Title(%1)", shows.size());

        CL_GUIThemePart listThemePart(result, "selection");
        CL_Font font = listThemePart.get_font();
        int padding = listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-right", "4")) + 
                        listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-left", "3")) + 5;
        
        int maxWidth = font.get_text_size(result->get_gc(), titleColumnName).width + padding;
        for (std::vector<ShowItem>::const_iterator it = shows.begin(); it != shows.end(); ++it)
        {            
            int textWidth = font.get_text_size(result->get_gc(), it->title).width + padding;
            if(maxWidth < textWidth) maxWidth = textWidth;

            ShowItem *showItemCopy = new ShowItem;
            *showItemCopy = *it;

            child.set_userdata(CL_SharedPtr<ShowItem>(showItemCopy));
            child.set_column_text(titleColumnId, it->title);
            child.set_column_text(ratingColumnId, CL_StringHelp::double_to_text(it->rating, 2));
            child.set_column_text(commentColumnId, clean(it->comment, CL_String("\r\n")));
            child = child.get_next_sibling();
        }
        if(maxWidth > 0) titleColumn.set_width(maxWidth);

        int ellipseWidth = font.get_text_size(result->get_gc(), "...  ").width + padding;
        commentColumn.set_width(result->get_width() - titleColumn.get_width()-ratingColumn.get_width()-ellipseWidth);
        
        while(child.is_null() == false)
        {
            child.set_userdata(CL_SharedPtr<ShowItem>());
            child.set_column_text(titleColumnId, "");
            child.set_column_text(ratingColumnId, "");
            child.set_column_text(commentColumnId, "");
            child = child.get_next_sibling();
        }

        titleColumn.set_caption(titleColumnName);
        update_current_page_number();
    }

    void on_previous_clicked()
    {
        if(currentPage > 0) currentPage--; 
        find_shows();
        populate_show_list();
    }

    void on_next_clicked()
    {
        currentPage++;
        if(find_shows() > 0)
        {
            populate_show_list();
        }
        else
        {
            currentPage--;
        }
    }

    void on_edit_clicked()
    {
        if(result->get_selected_item().is_null() == false)
        {
            CL_SharedPtr<ShowItem> show = cl_dynamic_pointer_cast<ShowItem>(result->get_selected_item().get_userdata());
            if(show)
            {
                tabMan->display_show_item(*show);
            }   
        }
    }

    void on_viewing_status_checked(VIEWING_STATUS status)
    {
        viewing_status_mask |= (1 << status);
        refresh_list();
    }

    void on_viewing_status_unchecked(VIEWING_STATUS status)
    {
        viewing_status_mask &= ~(1 << status);
        refresh_list();
    }

    void on_visiblity_changed(bool visible)
    {
        if(visible)
        {
            refresh_list();
        }
    }

public:
    ViewPage(CL_TabPage *page, TabManager *tabMan, const CL_SharedPtr<Database> &db)
        : Page(page->get_id()), tabMan(tabMan), database(db), currentPage(0), viewing_status_mask(0),
          pagenumber(CL_LineEdit::get_named_item(page, "pagenumber")),
          result(CL_ListView::get_named_item(page, "result")),
          search(CL_LineEdit::get_named_item(page, "search")),
          previous(CL_PushButton::get_named_item(page, "previous")),
          next(CL_PushButton::get_named_item(page, "next")),
          edit(CL_PushButton::get_named_item(page, "edit")),
          watching(CL_CheckBox::get_named_item(page, "watching")), 
          completed(CL_CheckBox::get_named_item(page, "completed")), 
          planning(CL_CheckBox::get_named_item(page, "planning")), 
          dropped(CL_CheckBox::get_named_item(page, "dropped"))
    {        
        result->show_detail_icon(false);
        result->show_detail_opener(false);

        page->func_visibility_change().set(this, &ViewPage::on_visiblity_changed);

        search->func_after_edit_changed().set(this, &ViewPage::on_search_edit);
        previous->func_clicked().set(this, &ViewPage::on_previous_clicked);
        next->func_clicked().set(this, &ViewPage::on_next_clicked);
        edit->func_clicked().set(this, &ViewPage::on_edit_clicked);

        watching->func_checked().set(this, &ViewPage::on_viewing_status_checked, WATCHING);
        completed->func_checked().set(this, &ViewPage::on_viewing_status_checked, COMPLETED);
        planning->func_checked().set(this, &ViewPage::on_viewing_status_checked, PLANNING);
        dropped->func_checked().set(this, &ViewPage::on_viewing_status_checked, DROPPED);

        watching->func_unchecked().set(this, &ViewPage::on_viewing_status_unchecked, WATCHING);
        completed->func_unchecked().set(this, &ViewPage::on_viewing_status_unchecked, COMPLETED);
        planning->func_unchecked().set(this, &ViewPage::on_viewing_status_unchecked, PLANNING);
        dropped->func_unchecked().set(this, &ViewPage::on_viewing_status_unchecked, DROPPED);

        result->get_icon_list().clear();
        result->set_multi_select(false);
        result->set_select_whole_row(true);

        pagenumber->set_alignment(CL_LineEdit::align_center);
        pagenumber->set_read_only(true);

        CL_ListViewColumnHeader column = result->get_header()->create_column("title", "Title");
        result->get_header()->append(column);


        CL_GUIThemePart listThemePart(result, "selection");
        int padding = listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-right", "4")) + 
            listThemePart.get_property_int(CL_GUIThemePartProperty("selection-margin-left", "3")) + 5;

        column = result->get_header()->create_column("rating", "Rating");
        result->get_header()->append(column);
        column.set_width(listThemePart.get_font().get_text_size(result->get_gc(), "Rating").width+padding);

        column = result->get_header()->create_column("comment", "Comment");
        result->get_header()->append(column);

        find_shows();
        populate_show_list();
    }
    virtual ~ViewPage(){}

};

TabManager::TabManager(CL_GUIComponent *parent, const CL_SharedPtr<Database> &database) 
    : tab(new CL_Tab(parent))
{
    CL_GUILayoutCorners layout;

    CL_TabPage *pageAdd = tab->add_page("Add Show", 0);
    CL_TabPage *pageView = tab->add_page("View Records",1);
    CL_TabPage *pageSearch = tab->add_page("Search MyAnimeList",2);
    pageAdd->set_layout(layout);
    pageView->set_layout(layout);
    pageSearch->set_layout(layout);

    // add start page
    pageAdd->create_components("add.gui");
    addPage.reset(new AddPage(pageAdd, this, database));
    // add end page

    // find/view records
    pageView->create_components("view.gui");
    viewPage.reset(new ViewPage(pageView, this, database));

    // myanimelist search page
    pageSearch->create_components("view.gui");
    searchPage.reset(new SearchPage(pageSearch, this, database));
}

CL_Tab *TabManager::get_tab() const 
{
    return tab;
}

class App
{
    typedef const std::vector<CL_String>& Args;

    std::auto_ptr<TabManager> tabMan;

    CL_SlotContainer slots;
    bool close_clicked;

    CL_SharedPtr<Database> database;

private:

    CL_DisplayWindowDescription get_desc() const
    {
        CL_DisplayWindowDescription desc;
        desc.set_allow_resize(false);
        desc.show_maximize_button(false);
        desc.set_size(CL_Size(800, 600), true);
        desc.set_title("Anime Record by Hunter");
        desc.set_visible(false);
        return desc;
    }


    bool on_close(CL_Window *win) 
    {
        win->exit_with_code(0); 
        close_clicked = true; 
        return true;
    }

    void on_resize(CL_Window *win)
    {
        tabMan->get_tab()->set_geometry(CL_Rect(0,0,win->get_size()));
    }

  
    void setup_window(CL_Window &win)
    {        
        tabMan.reset(new TabManager(&win, database));
                
        win.func_resized().set(this, &App::on_resize, &win);
    }

    int start(Args args)
    {
        database = CL_SharedPtr<Database>(new Database);

        CL_GUIManager guiMan("theme");

        CL_Window win(&guiMan, get_desc());
        CL_Rect client_area = win.get_client_area();
        win.func_close().set(this, &App::on_close, &win);

        CL_GUILayoutCorners layout;
        win.set_layout(layout);
        setup_window(win);
        win.set_visible();

        return guiMan.exec();
    }

public:
    App()
    {
        close_clicked = false;
    }
    static int main(Args args)
    {
#ifdef ENABLE_CONSOLE
        CL_ConsoleWindow console("Console", 150, 2000);
#endif

        try
        {
            // Initialize ClanLib base components
            CL_SetupCore setup_core;

            // Initialize the ClanLib display component
            CL_SetupDisplay setup_display;

            CL_SetupGUI setup_gui;

            CL_SetupNetwork setup_network;
            

#ifdef USE_SOFTWARE_RENDERER
            CL_SetupSWRender setup_swrender;
#endif

#ifdef USE_OPENGL_1
            CL_SetupGL1 setup_gl1;
#endif

#ifdef USE_OPENGL_2
            CL_SetupGL setup_gl;
#endif

            // Start the Application
            App app;
            int retval = app.start(args);
            return retval;
        }
        catch(CL_Exception &exception)
        {
            // Create a console window for text-output if not available
#ifndef ENABLE_CONSOLE
            CL_ConsoleWindow console("Console", 150, 2000);
#endif
            CL_Console::write_line("Exception caught: %1", exception.what());
            console.display_close_message();

            return -1;
        }
    }

};

// Instantiate CL_ClanApplication, informing it where the Program is located
static CL_ClanApplication app(&App::main);


