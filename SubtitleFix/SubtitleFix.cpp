// SubtitleFix.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <fstream>
#include <string>

#define SUBTITLE_BLOCK_NUM (1024)
#define MAX_LINE_LEN (68)

using namespace std;

struct subdata
{
    string line;
    string time;
    string content;
    bool skip;
    bool merged;
};

static int calc_timestamp(string& time)
{
    size_t offset = time.find(':', 0);
    string hour = time.substr(0, offset);

    offset++;
    size_t offset2 = time.find(':', offset);
    string min = time.substr(offset, offset2 - offset);

    offset2++;
    offset = time.find(',', offset2);
    string sec = time.substr(offset2, offset - offset2);

    offset++;
    offset2 = time.find(' ', offset);
    string msec = time.substr(offset, offset2 - offset);

    int time_int = atoi(hour.c_str()) * 3600 + atoi(min.c_str()) * 60 + atoi(sec.c_str());

    time_int *= 1000;
    time_int += atoi(msec.c_str());

    return time_int;
}

static int read_subdata(ifstream &ifin, int block_num, subdata *psd, size_t max_line_len)
{
    int num = 0;
    string strline;
    subdata* last_sd = 0;

    while (num < block_num) {
        psd->line = "";
        psd->time = "";
        psd->content = "";
        psd->skip = true;
        psd->merged = false;

        if (ifin.eof())
            break;

        //line NO.
        getline(ifin, psd->line);
        if (psd->line == "")
            break;
        //time info
        getline(ifin, psd->time);
        if (psd->time == "")
            break;
        //subtitle
        getline(ifin, psd->content);
        if (psd->content == "")
            break;

        //remove all "[xxxx]"
        size_t offset = psd->content.find('[');
        size_t offset_2;
        if (offset != string::npos) {
            offset_2 = psd->content.find(']', offset);
            if (offset_2 != string::npos) {
                //we ignore "[xxxx]"
                cout << "*****remove: " << psd->content << endl;
                psd->content.replace(offset, offset_2 - offset + 1, "");
            }
        }

        // remove:
        //  00:00:03,080 --> 00:00:05,660
        //  (upbeat music)
        offset = psd->content.find('(');
        if (offset != string::npos) {
            offset_2 = psd->content.find(')', offset);
            if (offset_2 != string::npos) {
                cout << "*****remove: " << psd->content << endl;
                psd->content.replace(offset, offset_2 - offset + 1, "");
            }
        }

        //
        //remove the head whitespace (whisper.cpp's bug)
        if (psd->content != "" && psd->content[0] == ' ')
            psd->content.erase(0, 1);
        // merge two continuous lines
        if (last_sd && !last_sd->merged) {
            size_t last_len = last_sd->content.length();
            size_t current_len = psd->content.length();
            size_t twoline_len = last_len + current_len + 1;

            //
            // We do not merge the following two lines
            //
            //1663
            //    01:06 : 08, 820 -- > 01:06 : 09, 320
            //    [INAUDIBLE]
            //
            //1664
            //   01:06 : 09, 320 -- > 01:06 : 14, 680
            //    Good question.

            // should match the max line length in our whisper.cpp settings
            if (last_len > 0 && current_len > 0 && twoline_len < max_line_len) {
                size_t last_size = last_sd->time.find_last_of(' ');
                size_t cur_size = psd->time.find_last_of(' ');
                string sub_time = psd->time.substr(cur_size, -1);

                last_sd->time.erase(last_size, -1);
                last_sd->time += ' ';
                last_sd->time += sub_time;

                last_sd->content += " ";
                last_sd->content += psd->content;
                last_sd->merged = true;

                // we must keep last line in merge mode
                last_sd->skip = false;

                psd->merged = true;
                cout << "*****merge: " << psd->content << endl;
            }
        }
        //blank line
        getline(ifin, strline);

        if (!psd->merged && psd->content != "")
            psd->skip = false;
        //
        //fix: 00:48:43,290 --> 00:46:59,520
        //
        int first_tm = calc_timestamp(psd->time);
        offset = psd->time.find_last_of(' ');
        string second_time_str = psd->time.substr(offset + 1, -1);
        int second_tm = calc_timestamp(second_time_str);

        if (second_tm <= first_tm)
            psd->skip = true;

        //
        // fix huge time span， we use 20 as threshold
        // 00:46:54,400 --> 00:48:43,290
        // This week's show was produced by Parth Shah and edited by Tara Boyle and Rain
        //
        if (second_tm > first_tm + 20000) {
            psd->skip = true;
            cout << "*****remove: " << psd->time << endl;
        }

        last_sd = psd;
        psd++;
        num++;
    }

    return num;
}

// try to remove the repeated content
static int shrink_subdata(subdata *psd, int block_num, string &last_content)
{
    int repeat_num = 0, write_count = 0;
    int repeat_threshold = 1;
    int i = 0;

    while (i < block_num) {
        if (!psd->content.compare(last_content))
            repeat_num++;
        else {
            repeat_num = 0;
            write_count++;
        }

        if (repeat_num >= repeat_threshold || !write_count)
            psd->skip = true;
        last_content = psd->content;
        psd++;
        i++;
    }

    return i;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cout << "Missing arguments: SubtitleFix input.srt output.srt" << std::endl;
        return -1;
    }
    const char* ifile = argv[1];
    const char* ofile = argv[2];
    ifstream ifin;
    ofstream ofout;
    string last_content;

    subdata *sda = new subdata[SUBTITLE_BLOCK_NUM];
    if (!sda) {
        cout << "Memory is not enough!\n";
        return -1;
    }

    ifin.open(ifile, ios::in);
    ofout.open(ofile, ios::out);

    if (!ifin.is_open()) {
        cout << "Failed to open file: " << ifile << endl;
        delete[] sda;
        return -1;
    }

    if (!ofout.is_open()) {
        delete[] sda;
        ifin.close();
        cout << "Failed to open file: " << ofile << endl;
        return - 1;
    }

    int sub_count = 0;
    do {
        if (ifin.eof())
            break;

        int block_num = read_subdata(ifin, SUBTITLE_BLOCK_NUM, sda, MAX_LINE_LEN);
        shrink_subdata(sda, block_num, last_content);

        int k = 0;
        subdata* psd = sda;
        while (k < block_num) {
            if (!psd->skip) {
                sub_count++;
                ofout << sub_count << endl;
                ofout << psd->time << endl;
                ofout << psd->content << endl;
                ofout << endl;

                last_content = psd->content;
                cout << "writing: " << last_content << endl;
            }
            psd->skip = true;
            psd++;

            k++;
        }
    } while (true);

    ifin.close();
    ofout.close();

    delete[] sda;

    return 0;
}
