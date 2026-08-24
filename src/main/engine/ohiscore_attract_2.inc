            video.write_tile32(&tile_addr, 0);
    }

    uint32_t attract_tile_address(uint16_t x, uint16_t y)
    {
        return ohud.translate(
            static_cast<uint16_t>(24 + x),
            y,
            0x10E000);
    }

    void write_attract_tile_text(uint16_t x,
                                 uint16_t y,
                                 const std::string& source)
    {
        for (size_t i = 0; i < source.size() && x + i < 40; i++)
        {
            const char value = source[i];
            const uint8_t tile =
                value == ' ' ? 0 : attract_tile_char(value);

            video.write_tile8(
                attract_tile_address(
                    static_cast<uint16_t>(x + i), y) + 1,
                tile);
        }
    }

    void clear_attract_tile_span(uint16_t x,
                                 uint16_t y,
                                 uint16_t width)
    {
        for (uint16_t i = 0; i < width && x + i < 40; i++)
        {
            video.write_tile8(
                attract_tile_address(
                    static_cast<uint16_t>(x + i), y) + 1,
                0);
        }
    }

    void write_attract_tile_row(int row, const std::string& source)
    {
        const uint16_t y = static_cast<uint16_t>(
            ATTRACT_ROW_BOTTOM_Y + (row * 2));

        clear_attract_tile_span(0, y, 40);
        write_attract_tile_text(0, y, source);
    }

    void shift_stock_rows_down_two_lines()
    {
        std::array<std::array<uint8_t, 40>, 10> staged{};

        for (int row = 0; row < 10; row++)
        {
            const uint16_t y = static_cast<uint16_t>(7 + row);
            for (int x = 0; x < 40; x++)
            {
                staged[row][x] = video.read_tile8(
                    attract_tile_address(
                        static_cast<uint16_t>(x), y) + 1);
            }
        }

        for (int row = 0; row < 10; row++)
        {
            const uint16_t y = static_cast<uint16_t>(9 + row);
            for (int x = 0; x < 40; x++)
            {
                video.write_tile8(
                    attract_tile_address(
                        static_cast<uint16_t>(x), y) + 1,
                    staged[row][x]);
            }
        }
    }

    enum FieldAlign
    {
        FIELD_LEFT,
        FIELD_CENTER,
        FIELD_RIGHT
    };

    void put_field(std::string& row,
                   int x,
                   int width,
                   const std::string& value,
                   FieldAlign align)
    {
        if (x < 0 || x >= 40 || width <= 0)
            return;

        width = std::min(width, 40 - x);
        std::string text = value;

        if (static_cast<int>(text.size()) > width)
            text.resize(width);

        int offset = 0;
        if (align == FIELD_CENTER)
            offset = (width - static_cast<int>(text.size())) / 2;
        else if (align == FIELD_RIGHT)
            offset = width - static_cast<int>(text.size());

        for (size_t i = 0; i < text.size(); i++)
            row[x + offset + static_cast<int>(i)] = text[i];
    }

    void draw_header_field(int x,
                           int width,
                           const char* text,
                           FieldAlign align = FIELD_CENTER)
    {
        std::string row(40, ' ');
        put_field(row, x, width, text, align);

        int first = 0;
        while (first < 40 && row[first] == ' ')
            first++;

        int last = 39;
        while (last >= first && row[last] == ' ')
            last--;

        if (first <= last)
        {
            draw_attract_text(
                static_cast<uint16_t>(first),
                7,
                row.substr(first, last - first + 1),
                OHud::GREEN);
        }
    }

    int bcd_to_decimal(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    void format_counter(uint16_t counter, char* dst, size_t size)
    {
        if (!counter)
        {
            std::snprintf(dst, size, "--'--\"--");
            return;
        }

        uint8_t converted[3] = {0, 0, 0};
        outils::convert_counter_to_time(counter, converted);

        std::snprintf(
            dst,
            size,
            "%02u'%02u\"%02u",
            static_cast<unsigned>(converted[0]),
            static_cast<unsigned>(bcd_to_decimal(converted[1])),
            static_cast<unsigned>(bcd_to_decimal(converted[2])));
    }

    void format_endless_time(uint32_t ticks, char* dst, size_t size)
    {
        if (!ticks)
        {
            std::snprintf(dst, size, "--'--\"--");
            return;
        }

        const uint64_t centiseconds =
            (static_cast<uint64_t>(ticks) * 100ULL) / 60ULL;
        const uint32_t minutes =
            static_cast<uint32_t>(centiseconds / 6000ULL);
        const uint32_t seconds =
            static_cast<uint32_t>((centiseconds / 100ULL) % 60ULL);
        const uint32_t hundredths =
            static_cast<uint32_t>(centiseconds % 100ULL);

        std::snprintf(
            dst,
            size,
            "%02u'%02u\"%02u",
            minutes,
            seconds,
            hundredths);
    }

    char read_initial(xml_parser::ptree& data, const std::string& key)
    {
        const std::string value = data.get_string(key, "_");
        return value.empty() || value[0] == '_' ? ' ' : value[0];
    }

    std::string initials_text(char i1, char i2, char i3)
    {
        std::string result;
        result += i1 == ' ' ? '-' : i1;
        result += i2 == ' ' ? '-' : i2;
        result += i3 == ' ' ? '-' : i3;
        return result;
    }

    void prepare_stock_record_rows(OHiScore& score)
    {
        // RECORD is the completion time stored in the normal score table.
        // Old/incomplete entries can legitimately have a zero counter. Show a
        // placeholder rather than leaving the whole column looking broken.
        for (int row = 0; row < ATTRACT_VISIBLE_ROWS; row++)
        {
            const uint16_t y = static_cast<uint16_t>(
                ATTRACT_ROW_BOTTOM_Y + (row * 2));

            char time_text[16];
            format_counter(
                score.scores[row].time,
                time_text,
                sizeof(time_text));

            clear_attract_tile_span(28, y, 10);
            write_attract_tile_text(29, y, time_text);
