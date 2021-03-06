#pragma once

#include "generator/osm_element.hpp"

#include "base/logging.hpp"
#include "base/stl_add.hpp"
#include "base/string_utils.hpp"

#include "std/fstream.hpp"
#include "std/map.hpp"
#include "std/set.hpp"
#include "std/string.hpp"
#include "std/utility.hpp"


class WaysParserHelper
{
public:
  WaysParserHelper(map<uint64_t, string> & ways) : m_ways(ways) {}

  void ParseStream(istream & input)
  {
    string oneLine;
    while (getline(input, oneLine, '\n'))
    {
      // String format: <<id;tag>>.
      auto pos = oneLine.find(';');
      if (pos != string::npos)
      {
        uint64_t wayId;
        CHECK(strings::to_uint64(oneLine.substr(0, pos), wayId),());
        m_ways[wayId] = oneLine.substr(pos + 1, oneLine.length() - pos - 1);
      }
    }
  }

private:
  map<uint64_t, string> & m_ways;
};

class CapitalsParserHelper
{
public:
  CapitalsParserHelper(set<uint64_t> & capitals) : m_capitals(capitals) {}

  void ParseStream(istream & input)
  {
    string oneLine;
    while (getline(input, oneLine, '\n'))
    {
      // String format: <<lat;lon;id;is_capital>>.
      // First ';'.
      auto pos = oneLine.find(";");
      if (pos != string::npos)
      {
        // Second ';'.
        pos = oneLine.find(";", pos + 1);
        if (pos != string::npos)
        {
          uint64_t nodeId;
          // Third ';'.
          auto endPos = oneLine.find(";", pos + 1);
          if (endPos != string::npos)
          {
            if (strings::to_uint64(oneLine.substr(pos + 1, endPos - pos - 1), nodeId))
              m_capitals.insert(nodeId);
          }
        }
      }
    }
  }

private:
  set<uint64_t> & m_capitals;
};

class TagAdmixer
{
public:
  TagAdmixer(string const & waysFile, string const & capitalsFile) : m_ferryTag("route", "ferry")
  {
    try
    {
      ifstream reader(waysFile);
      WaysParserHelper parser(m_ways);
      parser.ParseStream(reader);
    }
    catch (ifstream::failure const &)
    {
      LOG(LWARNING, ("Can't read the world level ways file! Generating world without roads. Path:", waysFile));
      return;
    }

    try
    {
      ifstream reader(capitalsFile);
      CapitalsParserHelper parser(m_capitals);
      parser.ParseStream(reader);
    }
    catch (ifstream::failure const &)
    {
      LOG(LWARNING, ("Can't read the world level capitals file! Generating world without towns admixing. Path:", capitalsFile));
      return;
    }
  }

  void operator()(OsmElement * e)
  {
    if (e->type == OsmElement::EntityType::Way && m_ways.find(e->id) != m_ways.end())
    {
      // Exclude ferry routes.
      if (find(e->Tags().begin(), e->Tags().end(), m_ferryTag) == e->Tags().end())
        e->AddTag("highway", m_ways[e->id]);
    }
    else if (e->type == OsmElement::EntityType::Node && m_capitals.find(e->id) != m_capitals.end())
    {
      // Our goal here - to make some capitals visible in World map.
      // The simplest way is to upgrade population to 45000,
      // according to our visibility rules in mapcss files.
      e->UpdateTag("population", [] (string & v)
      {
        uint64_t n;
        if (!strings::to_uint64(v, n) || n < 45000)
          v = "45000";
      });
    }
  }

private:
  map<uint64_t, string> m_ways;
  set<uint64_t> m_capitals;
  OsmElement::Tag const m_ferryTag;
};

class TagReplacer
{
  map<OsmElement::Tag, vector<string>> m_entries;
public:
  TagReplacer(string const & filePath)
  {
    ifstream stream(filePath);

    OsmElement::Tag tag;
    vector<string> values;
    string line;
    while (std::getline(stream, line))
    {
      if (line.empty())
        continue;

      strings::SimpleTokenizer iter(line, " \t=,:");
      if (!iter)
        continue;
      tag.key = *iter;
      ++iter;
      if (!iter)
        continue;
      tag.value = *iter;

      values.clear();
      while (++iter)
        values.push_back(*iter);

      if (values.size() >= 2 && values.size() % 2 == 0)
        m_entries[tag].swap(values);
    }
  }

  void operator()(OsmElement * p)
  {
    for (auto & tag : p->m_tags)
    {
      auto it = m_entries.find(tag);
      if (it != m_entries.end())
      {
        auto const & v = it->second;
        tag.key = v[0];
        tag.value = v[1];
        for (size_t i = 2; i < v.size(); i += 2)
          p->AddTag(v[i], v[i + 1]);
      }
    }
  }
};

class OsmTagMixer
{
  map<pair<OsmElement::EntityType, uint64_t>, vector<OsmElement::Tag>> m_elements;

public:
  OsmTagMixer(string const & filePath)
  {
    ifstream stream(filePath);
    vector<string> values;
    vector<OsmElement::Tag> tags;
    string line;
    while (std::getline(stream, line))
    {
      if (line.empty() || line.front() == '#')
        continue;

      strings::ParseCSVRow(line, ',', values);
      if (values.size() < 3)
        continue;

      OsmElement::EntityType entityType = OsmElement::StringToEntityType(values[0]);
      uint64_t id;
      if (entityType == OsmElement::EntityType::Unknown || !strings::to_uint64(values[1], id))
        continue;

      for (size_t i = 2; i < values.size(); ++i)
      {
        auto p = values[i].find('=');
        if (p != string::npos)
          tags.push_back(OsmElement::Tag(values[i].substr(0, p), values[i].substr(p + 1)));
      }

      if (!tags.empty())
      {
        pair<OsmElement::EntityType, uint64_t> elementPair = {entityType, id};
        m_elements[elementPair].swap(tags);
      }
    }
  }

  void operator()(OsmElement * p)
  {
    pair<OsmElement::EntityType, uint64_t> elementId = {p->type, p->id};
    auto elements = m_elements.find(elementId);
    if (elements != m_elements.end())
    {
      for (OsmElement::Tag tag : elements->second)
        p->AddTag(tag.key, tag.value);
    }
  }
};
