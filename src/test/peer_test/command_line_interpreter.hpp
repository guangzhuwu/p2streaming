//  Boost cli library command_line_interpreter.hpp header file  --------------//

//  (C) Copyright Jean-Daniel Michaud 2007. Permission to copy, use, modify, 
//  sell and distribute this software is granted provided this copyright notice 
//  appears in all copies. This software is provided "as is" without express or 
//  implied warranty, and with no claim as to its suitability for any purpose.

//  See http://www.boost.org/LICENSE_1_0.txt for licensing.
//  See http://code.google.com/p/clipo/ for library home page.

#ifndef BOOST_COMMAND_LINE_INTERPRETER
#define BOOST_COMMAND_LINE_INTERPRETER

#include <vector>
#include <string>
#include <iostream>
#include <iterator>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>

namespace boost { namespace cli {

typedef boost::program_options::options_description commands_description;

/*
 * Unshamefully copied from the split_winmain function developed by Vladimir Prus 
 */ 
static std::vector<std::string> split_command_line(const std::string& input)
{
  std::vector<std::string> result;

  std::string::const_iterator i = input.begin(), e = input.end();
  for(;i != e; ++i)
    if (!isspace((unsigned char)*i))
      break;
 
  if (i != e) 
  {
    std::string current;
    bool inside_quoted = false;
    int backslash_count = 0;
    
    for(; i != e; ++i) 
    {
      if (*i == '"') 
      {
        // '"' preceded by even number (n) of backslashes generates
        // n/2 backslashes and is a quoted block delimiter
        if (backslash_count % 2 == 0) 
        {
          current.append(backslash_count / 2, '\\');
          inside_quoted = !inside_quoted;
          // '"' preceded by odd number (n) of backslashes generates
          // (n-1)/2 backslashes and is literal quote.
        } 
        else 
        {
          current.append(backslash_count / 2, '\\');                
          current += '"';                
        }
        backslash_count = 0;
      } 
      else if (*i == '\\') 
      {
        ++backslash_count;
      } else 
      {
        // Not quote or backslash. All accumulated backslashes should be
        // added
        if (backslash_count) 
        {
          current.append(backslash_count, '\\');
          backslash_count = 0;
        }
        if (isspace((unsigned char)*i) && !inside_quoted) 
        {
          // Space outside quoted section terminate the current argument
          result.push_back(current);
          current.resize(0);
          for(;i != e && isspace((unsigned char)*i); ++i)
            ;
          --i;
        } 
        else 
        {                  
          current += *i;
        }
      }
    }

    // If we have trailing backslashes, add them
    if (backslash_count)
      current.append(backslash_count, '\\');

    // If we have non-empty 'current' or we're still in quoted
    // section (even if 'current' is empty), add the last token.
    if (!current.empty() || inside_quoted)
      result.push_back(current);        
  }
  return result;
}

class command_line_interpreter
{
public:
  command_line_interpreter(const commands_description& desc)
  {
    m_desc = &desc;
  }

  command_line_interpreter(const commands_description& desc, 
                           const std::string &prompt)
  {
    m_desc = &desc;
    m_prompt = prompt;
  }

  void handle_read_line(std::string line)
  {
    std::vector<std::string> args;
    
    // huu, ugly...
    args = split_command_line(std::string("--") + line); 
    
    try
    {
      boost::program_options::variables_map vm;
      boost::program_options::store(
        boost::program_options::command_line_parser(args).options(*m_desc).run(), 
        vm);
      boost::program_options::notify(vm);
    }
    catch (boost::program_options::unknown_option &e) 
    {
      std::cerr << "error: " << e.what() << std::endl;
    }
    catch (boost::program_options::invalid_command_line_syntax &e)
    {
      std::cerr << "error: " << e.what() << std::endl;
    }
    catch (boost::program_options::validation_error &e)
    {
      std::cerr << "error: " << e.what() << std::endl;
    }
  }

  void interpret(std::istream &input_stream)
  {
    std::string command;
    std::cout << m_prompt << std::flush;

    while (std::getline(input_stream, command, '\n'))
    {
      handle_read_line(command);
      std::cout << m_prompt << std::flush;
    }
  }

  typedef char *(*realine_function_pointer_t)(const char *);

  void interpret_(std::istream &input_stream, 
                  realine_function_pointer_t f)
  {
    char        *line = NULL;

    while (line = boost::bind(f, m_prompt.c_str())())
    {
      if (!line)
        continue ;
        
      std::string command = line;
      free(line);
      handle_read_line(command);
    }
  }
  

private:
  const commands_description* m_desc;
  std::string                 m_prompt;
};

} // !namespace cli
} // !namespace boost

#endif // !BOOST_COMMAND_LINE_INTERPRETER
