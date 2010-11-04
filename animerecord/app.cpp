#include <ClanLib/core.h>
#include <ClanLib/application.h>
#include <ClanLib/display.h>
#include <ClanLib/gui.h>
#include <ClanLib/database.h>
#include <ClanLib/sqlite.h>
#include <algorithm>
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


template<class StrType>
StrType trimmed( StrType const& str, char const* sepSet=" \n\r\t")
{
    StrType::size_type const first = str.find_first_not_of(sepSet);
    return ( first==StrType::npos) ? StrType() : str.substr(first, str.find_last_not_of(sepSet)-first+1);
}

template<class StrType>
StrType clean (const StrType &oldStr, const StrType &bad) 
{
    StrType::iterator s, d;
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

struct SearchQuery
{
    CL_String name;
    int start;
    int limit;
};

struct GenreItemCheckbox
{
    CL_CheckBox *box;
    int id;
};

struct GenreItem
{
    int id;
    CL_String name;
};

struct ShowItem
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
    int rating;
    CL_String comment;
};


class Database
{    
    CL_SharedPtr<CL_DBConnection> sql;
        
    template<typename StrType>
    StrType strip_sql_symbol(const StrType &s) const
    {
        return clean(s, StrType("%_"));
    }


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

    // returns the inserted show id
    int add_show_to_db(const CL_String &title, const CL_String &type, const std::vector<GenreItemCheckbox> &genres, int year, int rating, const CL_String &comment, int episodes, int season)
    {
        CL_String title_s = strip_sql_symbol(title);
        CL_String type_s = strip_sql_symbol(type);
        CL_String comment_s = strip_sql_symbol(comment);

        if(title_s.empty())
            throw CL_Exception("title is empty!");

        CL_DBTransaction transaction = sql->begin_transaction();

        CL_DBCommand cmd = sql->create_command("insert into show (title, type, year, rating, comment, episodes, season) values (?1,?2,?3,?4,?5,?6,?7)",
            title_s, type_s, year, rating, comment_s, episodes, season);
        sql->execute_non_query(cmd);

        int showid = cmd.get_output_last_insert_rowid();

        cmd = sql->create_command("insert into show_genre (show_id, genre_id) values (?1,?2)");
        cmd.set_input_parameter(1, showid);

        for (std::vector<GenreItemCheckbox>::const_iterator it = genres.begin(); it != genres.end(); ++it)
        {
            cmd.set_input_parameter(2, it->id);
            sql->execute_non_query(cmd);
        }

        transaction.commit();

        return showid;
    }

    void update_show(int showid, const CL_String &title, const CL_String &type, const std::vector<GenreItemCheckbox> &genres, int year, int rating, const CL_String &comment, int episodes, int season)
    {
        CL_String title_s = strip_sql_symbol(title);
        CL_String type_s = strip_sql_symbol(type);
        CL_String comment_s = strip_sql_symbol(comment);

        if(title_s.empty())
            throw CL_Exception("title is empty!");

        CL_DBTransaction transaction = sql->begin_transaction();

        CL_DBCommand cmd = sql->create_command("update show set title=?2, type=?3, year=?4, rating=?5, comment=?6, episodes=?7, season=?8 where id=?1",
                                               showid, title_s, type_s, year, rating, comment_s, episodes, season);
        sql->execute_non_query(cmd);

        cmd = sql->create_command("delete from show_genre where show_id=?1", showid);
        sql->execute_non_query(cmd);

        cmd = sql->create_command("insert into show_genre (show_id, genre_id) values (?1,?2)");
        cmd.set_input_parameter(1, showid);

        for (std::vector<GenreItemCheckbox>::const_iterator it = genres.begin(); it != genres.end(); ++it)
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

    ShowItem find_show(int id)
    {
        ShowItem show;

        CL_DBCommand cmd = sql->create_command("select id, date_added, date_updated, title, type, year, episodes, season, rating, comment from show where id = ?1", id);
        CL_DBReader reader = sql->execute_reader(cmd);

        if(reader.retrieve_row())
        {
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

            reader.close();

            show.genres = find_show_genres(id);
        }

        return show;
    }

    // TODO full text search
    std::vector<ShowItem> find_shows(const CL_String &title=CL_String(), int start=0, int limit=20)
    {
        std::vector<ShowItem> shows;

        CL_DBCommand cmd;

        if(start == -1 && limit == -1)
        {
            cmd = sql->create_command("select id, date_added, date_updated, title, type, year, episodes, season, rating, comment from show "
                                       "order by title COLLATE NOCASE ");
        }
        else
        {
            cmd = sql->create_command("select id, date_added, date_updated, title, type, year, episodes, season, rating, comment from show where title like ?1 "
                                      "order by title COLLATE NOCASE " 
                                      "limit ?2, ?3", title.empty() ? "%" : title, start, limit);
        }

    
        CL_DBReader reader = sql->execute_reader(cmd);

        while(reader.retrieve_row())
        {
            ShowItem item;

            item.id = reader.get_column_value("id");
            item.date_added = reader.get_column_value("date_added");
            item.date_updated = reader.get_column_value("date_updated");
            item.title = reader.get_column_value("title");
            item.type = reader.get_column_value("type");
            item.year = reader.get_column_value("year");
            item.episodes = reader.get_column_value("episodes");
            item.season = reader.get_column_value("season");
            item.rating = reader.get_column_value("rating");
            item.comment = reader.get_column_value("comment");

            shows.push_back(item);           
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
        return find_shows("", -1, -1);
    }
};



class AddPage
{
    CL_TabPage *page;
    std::vector<GenreItemCheckbox> genreItems;
    CL_SharedPtr<Database> database;

public:
    AddPage(CL_TabPage *page, const CL_SharedPtr<Database> &database) : page(page), database(database)
    {
        CL_LineEdit::get_named_item(page, "title");
        CL_Spin &year = *CL_Spin::get_named_item(page, "year");
        CL_Frame &genreFrame = *CL_Frame::get_named_item(page, "genreFrame");
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_Label &ratingLabel = *CL_Label::get_named_item(page, "ratingLabel");
        CL_LineEdit &comment = *CL_LineEdit::get_named_item(page, "comment");
        CL_PushButton &addButton = *CL_PushButton::get_named_item(page, "add");
        CL_PushButton &clearButton = *CL_PushButton::get_named_item(page, "clear");
        CL_PushButton &searchButton = *CL_PushButton::get_named_item(page, "search");
        CL_PushButton &updateButton = *CL_PushButton::get_named_item(page, "update");
        CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
        CL_Spin &season = *CL_Spin::get_named_item(page, "season");

        CL_RadioButton &animeChoice = *CL_RadioButton::get_named_item(page, "animeChoice");
        CL_RadioButton &filmChoice = *CL_RadioButton::get_named_item(page, "filmChoice");
        CL_RadioButton &tvChoice = *CL_RadioButton::get_named_item(page, "tvChoice");

        CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");
        CL_LineEdit &date_added = *CL_LineEdit::get_named_item(page, "date_added");
        CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");

        id.set_read_only(true);
        date_added.set_read_only(true);
        date_updated.set_read_only(true);

        animeChoice.set_selected(true);

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

        std::vector<GenreItem> genres = this->database->get_all_genres();
        int xPos=10, yPos=15;
        for(std::vector<GenreItem>::const_iterator it = genres.begin(); it != genres.end(); ++it)
        {
            GenreItemCheckbox item;
            item.id = it->id;
            item.box = new CL_CheckBox(page);
            item.box->set_text(it->name);
            genreItems.push_back(item);

            CL_Rect genreRect = genreFrame.get_geometry();

            item.box->set_geometry(CL_Rect(genreRect.left+xPos, genreRect.top+yPos, CL_Size(100,20)));

            xPos += 100;
            if(xPos >= genreRect.right-100)
            {
                xPos = 10;
                yPos += 20;
            }

        }
    }

private:

    std::vector<GenreItemCheckbox> get_selected_genres() const
    {
        std::vector<GenreItemCheckbox> genres;
        for (std::vector<GenreItemCheckbox>::const_iterator it = genreItems.begin(); it != genreItems.end(); ++it)
        {
            if(it->box->is_checked())
            {
                genres.push_back(*it);
            }
        }

        return genres;
    }

    void clear_genre() const
    {
        for (std::vector<GenreItemCheckbox>::const_iterator it = genreItems.begin(); it != genreItems.end(); ++it)
        {
            it->box->set_checked(false);
        }
    }


    void on_rating_changed()
    {
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_Label &ratingLabel = *CL_Label::get_named_item(page, "ratingLabel");

        ratingLabel.set_text(cl_format("%1/10", rating.get_position()));
    }

    CL_String get_media_type() const
    {
        CL_RadioButton &animeChoice = *CL_RadioButton::get_named_item(page, "animeChoice");
        CL_RadioButton &filmChoice = *CL_RadioButton::get_named_item(page, "filmChoice");
        CL_RadioButton &tvChoice = *CL_RadioButton::get_named_item(page, "tvChoice");

        if(animeChoice.is_selected())
            return "Anime";
        else if(filmChoice.is_selected())
            return "Film";
        else if(tvChoice.is_selected())
            return "TV";

        return "";
    }

    void set_media_type(const CL_String &type) const
    {
        CL_RadioButton &animeChoice = *CL_RadioButton::get_named_item(page, "animeChoice");
        CL_RadioButton &filmChoice = *CL_RadioButton::get_named_item(page, "filmChoice");
        CL_RadioButton &tvChoice = *CL_RadioButton::get_named_item(page, "tvChoice");

        if(type == "Anime")
        {
            animeChoice.set_selected(true);          
            animeChoice.set_focus(true);
        }
        else if(type == "Film")
        {
            filmChoice.set_selected(true);
            filmChoice.set_focus(true);
        }
        else if(type == "TV")
        {
            tvChoice.set_selected(true);
            tvChoice.set_focus(true);
        }
        else reset_media_type();
    }

    void reset_media_type() const
    {
        CL_RadioButton &animeChoice = *CL_RadioButton::get_named_item(page, "animeChoice");
        animeChoice.set_selected(true);
        animeChoice.set_focus(true);
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
            CL_LineEdit &comment = *CL_LineEdit::get_named_item(page, "comment");
            CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
            CL_Spin &season = *CL_Spin::get_named_item(page, "season");

            CL_String trimmedTitle = clean(CL_String(trimmed(title.get_text())), CL_String("%_"));

            if(trimmedTitle.empty())
            {
                MessageDialog(page, "Error", "The Title cannot be blank").exec();
            }
            else if(episodes.get_value() <= 0)
            {
                MessageDialog(page, "Error", "The number of Episodes must be greater than 0").exec();
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

                        int showid = database->add_show_to_db(trimmedTitle, get_media_type(), get_selected_genres(), year.get_value(), 
                                                 rating.get_position(), comment.get_text(), episodes.get_value(), season.get_value());
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
            CL_LineEdit &comment = *CL_LineEdit::get_named_item(page, "comment");
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
            else if(episodes.get_value() <= 0)
            {
                MessageDialog(page, "Error", "The number of Episodes must be greater than 0").exec();
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
                            rating.get_position(), comment.get_text(), episodes.get_value(), season.get_value());
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
        CL_LineEdit &comment = *CL_LineEdit::get_named_item(page, "comment");
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

        reset_media_type();
        clear_genre();
    }

    void on_result_menu_click(ShowItem show)
    {
        CL_LineEdit &id = *CL_LineEdit::get_named_item(page, "id");
        CL_LineEdit &date_added = *CL_LineEdit::get_named_item(page, "date_added");
        CL_LineEdit &date_updated = *CL_LineEdit::get_named_item(page, "date_updated");
        CL_LineEdit &title = *CL_LineEdit::get_named_item(page, "title");
        CL_Spin &year = *CL_Spin::get_named_item(page, "year");
        CL_Slider &rating = *CL_Slider::get_named_item(page, "rating");
        CL_LineEdit &comment = *CL_LineEdit::get_named_item(page, "comment");
        CL_Spin &episodes = *CL_Spin::get_named_item(page, "episodes");
        CL_Spin &season = *CL_Spin::get_named_item(page, "season");

        id.set_text(cl_format("%1", show.id));
        id.request_repaint();
        date_added.set_text(show.date_added.to_local().to_short_datetime_string());
        date_added.request_repaint();
        date_updated.set_text(show.date_updated.to_local().to_short_datetime_string());
        date_updated.request_repaint();
        title.set_text(show.title);
        year.set_value(show.year);
        rating.set_position(show.rating);
        if(rating.func_value_changed().is_null() == false)
            rating.func_value_changed().invoke();
        comment.set_text(show.comment);
        episodes.set_value(show.episodes);
        season.set_value(show.season);
        set_media_type(show.type);
        clear_genre();

        for (std::vector<GenreItemCheckbox>::const_iterator it = genreItems.begin(); it != genreItems.end(); ++it)
        {
            for(std::vector<GenreItem>::const_iterator it2 = show.genres.begin(); it2 != show.genres.end(); ++it2)
            {
                if(it2->id == it->id)
                {
                    it->box->set_checked(true);
                }
            }
        }
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
        std::vector<ShowItem> shows = database->find_shows(trimmedTitle, start, limit);

        CL_PopupMenu pop;
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

class ViewPage
{
    std::vector<ShowItem> shows;
    CL_SharedPtr<Database> database;

    CL_ListView *result;
    CL_LineEdit *search;
    CL_PushButton *previous, *next;
    CL_LineEdit *pagenumber;

    const unsigned int LIMIT;
    unsigned int currentPage;
    CL_String query;

    void on_search_edit(CL_InputEvent &ev)
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
        shows = database->find_shows(query, currentPage*LIMIT, LIMIT);

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

            child.set_column_text(titleColumnId, it->title);
            child.set_column_text(ratingColumnId, cl_format("%1", it->rating));
            child.set_column_text(commentColumnId, it->comment);
            child = child.get_next_sibling();
        }
        if(maxWidth > 0) titleColumn.set_width(maxWidth);
        
        while(child.is_null() == false)
        {
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

public:
    ViewPage(CL_TabPage *page, const CL_SharedPtr<Database> &db) 
        : database(db), LIMIT(100), currentPage(0),
          pagenumber(CL_LineEdit::get_named_item(page, "pagenumber")),
          result(CL_ListView::get_named_item(page, "result")),
          search(CL_LineEdit::get_named_item(page, "search")),
          previous(CL_PushButton::get_named_item(page, "previous")),
          next(CL_PushButton::get_named_item(page, "next"))
    {
        search->func_after_edit_changed().set(this, &ViewPage::on_search_edit);
        previous->func_clicked().set(this, &ViewPage::on_previous_clicked);
        next->func_clicked().set(this, &ViewPage::on_next_clicked);

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

};

class App
{
    typedef const std::vector<CL_String>& Args;

    CL_Tab *tab;

    CL_SlotContainer slots;
    bool close_clicked;

    CL_SharedPtr<Database> database;


    // pages
    CL_AutoPtr<AddPage> addPage;
    CL_AutoPtr<ViewPage> viewPage;

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
        tab->set_geometry(CL_Rect(0,0,win->get_size()));
    }

  
    void setup_window(CL_Window &win)
    {
        CL_GUILayoutCorners layout;
        tab = new CL_Tab(&win);
        
        CL_TabPage *pageAdd = tab->add_page("Add Show", 0);
        pageAdd->set_layout(layout);
        CL_TabPage *pageView = tab->add_page("View Records",1);
        pageView->set_layout(layout);

        // add start page
        pageAdd->create_components("add.gui");
        addPage = new AddPage(pageAdd, database);
        // add end page

        // find/view records
        pageView->create_components("view.gui");
        viewPage = new ViewPage(pageView, database);


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

        guiMan.exec();
        return 0;
    }

public:
    App()
    {
        close_clicked = false;
    }
    static int main(Args args)
    {
#ifdef ENABLE_CONSOLE
        CL_ConsoleWindow console("Console", 80, 160);
#endif

        try
        {
            // Initialize ClanLib base components
            CL_SetupCore setup_core;

            // Initialize the ClanLib display component
            CL_SetupDisplay setup_display;

            CL_SetupGUI setup_gui;
            

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
            CL_ConsoleWindow console("Console", 80, 160);
#endif
            CL_Console::write_line("Exception caught: %1", exception.what());
            console.display_close_message();

            return -1;
        }
    }

};

// Instantiate CL_ClanApplication, informing it where the Program is located
static CL_ClanApplication app(&App::main);


