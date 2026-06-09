#include <scwx/qt/config/county_database.hpp>
#include <scwx/qt/settings/audio_settings.hpp>
#include <scwx/qt/settings/settings_definitions.hpp>
#include <scwx/qt/settings/settings_variable.hpp>
#include <scwx/qt/types/alert_types.hpp>
#include <scwx/qt/types/location_types.hpp>
#include <scwx/qt/types/media_types.hpp>

#include <boost/algorithm/string.hpp>
#include <fmt/format.h>

namespace scwx::qt::settings
{

static const std::string logPrefix_ = "scwx::qt::settings::audio_settings";

static const bool              kDefaultAlertEnabled_ {false};
static const awips::Phenomenon kDefaultPhenomenon_ {awips::Phenomenon::Unknown};

class AudioSettings::Impl
{
public:
   explicit Impl()
   {
      std::string defaultAlertSoundFileValue =
         types::GetMediaPath(types::AudioFile::EasAttentionSignal);
      std::string defaultAlertLocationMethodValue =
         types::GetLocationMethodName(types::LocationMethod::Fixed);

      boost::to_lower(defaultAlertLocationMethodValue);

      // SetDefault, SetMinimum and SetMaximum are descriptive
      // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
      alertSoundFile_.SetDefault(defaultAlertSoundFileValue);
      alertLocationMethod_.SetDefault(defaultAlertLocationMethodValue);
      alertLatitude_.SetDefault(0.0);
      alertLongitude_.SetDefault(0.0);
      alertRadius_.SetDefault(0.0);
      alertRadarSite_.SetDefault("default");
      alertWFO_.SetDefault("");
      ignoreMissingCodecs_.SetDefault(false);
      masterVolume_.SetDefault(100);
      alertOnlyNew_.SetDefault(false);

      alertLatitude_.SetMinimum(-90.0);
      alertLatitude_.SetMaximum(90.0);
      alertLongitude_.SetMinimum(-180.0);
      alertLongitude_.SetMaximum(180.0);
      alertRadius_.SetMinimum(0.0);
      alertRadius_.SetMaximum(9999999999);
      masterVolume_.SetMinimum(0);
      masterVolume_.SetMaximum(100);
      // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

      alertLocationMethod_.SetValidator(
         SCWX_SETTINGS_ENUM_VALIDATOR(types::LocationMethod,
                                      types::LocationMethodIterator(),
                                      types::GetLocationMethodName));

      alertCounty_.SetValidator(
         [](const std::string& value)
         {
            // Empty, or county exists in the database
            return value.empty() ||
                   config::CountyDatabase::GetCountyName(value) != value;
         });

      alertWFO_.SetValidator(
         [](const std::string& value)
         {
            return value.empty() ||
                   config::CountyDatabase::GetWFOs().count(value) != 0;
         });

      auto& alertAudioPhenomena = types::GetAlertAudioPhenomena();
      alertEnabled_.reserve(alertAudioPhenomena.size() + 1);
      alertSoundFiles_.reserve(alertAudioPhenomena.size() + 1);

      for (auto& phenomenon : alertAudioPhenomena)
      {
         std::string phenomenonCode = awips::GetPhenomenonCode(phenomenon);
         std::string enabledName    = fmt::format("{}_enabled", phenomenonCode);
         std::string soundName = fmt::format("{}_sound_file", phenomenonCode);

         auto enabledResult = alertEnabled_.emplace(
            phenomenon, SettingsVariable<bool> {enabledName});

         SettingsVariable<bool>& enabledVariable = enabledResult.first->second;
         enabledVariable.SetDefault(kDefaultAlertEnabled_);
         variables_.push_back(&enabledVariable);

         auto soundResult = alertSoundFiles_.emplace(
            phenomenon, SettingsVariable<std::string> {soundName});

         SettingsVariable<std::string>& soundVariable =
            soundResult.first->second;
         soundVariable.SetDefault("");
         variables_.push_back(&soundVariable);
      }

      tornadoBaseSoundFile_.SetDefault("");
      tornadoConsiderableSoundFile_.SetDefault("");
      tornadoCatastrophicSoundFile_.SetDefault("");
      tornadoObservedSoundFile_.SetDefault("");

      variables_.push_back(&tornadoBaseSoundFile_);
      variables_.push_back(&tornadoConsiderableSoundFile_);
      variables_.push_back(&tornadoCatastrophicSoundFile_);
      variables_.push_back(&tornadoObservedSoundFile_);
      variables_.push_back(&alertOnlyNew_);

      // Create a default disabled alert, not stored in the settings file
      alertEnabled_.emplace(kDefaultPhenomenon_,
                            SettingsVariable<bool> {"alert_disabled"});
      alertSoundFiles_.emplace(
         kDefaultPhenomenon_,
         SettingsVariable<std::string> {"alert_sound_disabled"});
   }

   ~Impl()                       = default;
   Impl(const Impl&)             = delete;
   Impl& operator=(const Impl&)  = delete;
   Impl(const Impl&&)            = delete;
   Impl& operator=(const Impl&&) = delete;

   SettingsVariable<std::string> alertSoundFile_ {"alert_sound_file"};
   SettingsVariable<std::string> alertLocationMethod_ {"alert_location_method"};
   SettingsVariable<double>      alertLatitude_ {"alert_latitude"};
   SettingsVariable<double>      alertLongitude_ {"alert_longitude"};
   SettingsVariable<std::string> alertRadarSite_ {"alert_radar_site"};
   SettingsVariable<double>      alertRadius_ {"alert_radius"};
   SettingsVariable<std::string> alertCounty_ {"alert_county"};
   SettingsVariable<std::string> alertWFO_ {"alert_wfo"};
   SettingsVariable<bool>        ignoreMissingCodecs_ {"ignore_missing_codecs"};
   SettingsVariable<bool>         alertOnlyNew_ {"alert_only_new"};
   SettingsVariable<std::int64_t> masterVolume_ {"master_volume"};

   SettingsVariable<std::string> tornadoBaseSoundFile_ {"to_base_sound_file"};
   SettingsVariable<std::string> tornadoConsiderableSoundFile_ {
      "to_considerable_sound_file"};
   SettingsVariable<std::string> tornadoCatastrophicSoundFile_ {
      "to_catastrophic_sound_file"};
   SettingsVariable<std::string> tornadoObservedSoundFile_ {
      "to_observed_sound_file"};

   std::unordered_map<awips::Phenomenon, SettingsVariable<bool>>
                                      alertEnabled_ {};
   std::unordered_map<awips::Phenomenon, SettingsVariable<std::string>>
                                      alertSoundFiles_ {};
   std::vector<SettingsVariableBase*> variables_ {};
};

AudioSettings::AudioSettings() :
    SettingsCategory("audio"), p(std::make_unique<Impl>())
{
   RegisterVariables({&p->alertSoundFile_,
                      &p->alertLocationMethod_,
                      &p->alertLatitude_,
                      &p->alertLongitude_,
                      &p->alertRadarSite_,
                      &p->alertRadius_,
                      &p->alertCounty_,
                      &p->alertWFO_,
                      &p->ignoreMissingCodecs_,
                      &p->masterVolume_,
                      &p->alertOnlyNew_,
                      &p->tornadoBaseSoundFile_,
                      &p->tornadoConsiderableSoundFile_,
                      &p->tornadoCatastrophicSoundFile_,
                      &p->tornadoObservedSoundFile_});
   RegisterVariables(p->variables_);
   SetDefaults();

   p->variables_.clear();
}
AudioSettings::~AudioSettings() = default;

AudioSettings::AudioSettings(AudioSettings&&) noexcept            = default;
AudioSettings& AudioSettings::operator=(AudioSettings&&) noexcept = default;

SettingsVariable<std::string>& AudioSettings::alert_sound_file() const
{
   return p->alertSoundFile_;
}

SettingsVariable<std::string>& AudioSettings::alert_location_method() const
{
   return p->alertLocationMethod_;
}

SettingsVariable<double>& AudioSettings::alert_latitude() const
{
   return p->alertLatitude_;
}

SettingsVariable<double>& AudioSettings::alert_longitude() const
{
   return p->alertLongitude_;
}

SettingsVariable<std::string>& AudioSettings::alert_radar_site() const
{
   return p->alertRadarSite_;
}

SettingsVariable<double>& AudioSettings::alert_radius() const
{
   return p->alertRadius_;
}

SettingsVariable<std::string>& AudioSettings::alert_county() const
{
   return p->alertCounty_;
}

SettingsVariable<std::string>& AudioSettings::alert_wfo() const
{
   return p->alertWFO_;
}

SettingsVariable<bool>&
AudioSettings::alert_enabled(awips::Phenomenon phenomenon) const
{
   auto alert = p->alertEnabled_.find(phenomenon);
   if (alert == p->alertEnabled_.cend())
   {
      alert = p->alertEnabled_.find(kDefaultPhenomenon_);
   }
   return alert->second;
}

SettingsVariable<std::string>&
AudioSettings::alert_sound_file(awips::Phenomenon phenomenon) const
{
   auto alert = p->alertSoundFiles_.find(phenomenon);
   if (alert != p->alertSoundFiles_.cend())
   {
      return alert->second;
   }

   // Fallback for unknown phenomenon
   return p->alertSoundFile_;
}

SettingsVariable<std::string>& AudioSettings::tornado_base_sound_file() const
{
   return p->tornadoBaseSoundFile_;
}

SettingsVariable<std::string>&
AudioSettings::tornado_considerable_sound_file() const
{
   return p->tornadoConsiderableSoundFile_;
}

SettingsVariable<std::string>&
AudioSettings::tornado_catastrophic_sound_file() const
{
   return p->tornadoCatastrophicSoundFile_;
}

SettingsVariable<std::string>&
AudioSettings::tornado_observed_sound_file() const
{
   return p->tornadoObservedSoundFile_;
}

SettingsVariable<bool>& AudioSettings::ignore_missing_codecs() const
{
   return p->ignoreMissingCodecs_;
}

SettingsVariable<bool>& AudioSettings::alert_only_new() const
{
   return p->alertOnlyNew_;
}

SettingsVariable<std::int64_t>& AudioSettings::master_volume() const
{
   return p->masterVolume_;
}

AudioSettings& AudioSettings::Instance()
{
   static AudioSettings audioSettings_;
   return audioSettings_;
}

bool operator==(const AudioSettings& lhs, const AudioSettings& rhs)
{
   return (lhs.p->alertSoundFile_ == rhs.p->alertSoundFile_ &&
           lhs.p->alertLocationMethod_ == rhs.p->alertLocationMethod_ &&
           lhs.p->alertLatitude_ == rhs.p->alertLatitude_ &&
           lhs.p->alertLongitude_ == rhs.p->alertLongitude_ &&
           lhs.p->alertRadarSite_ == rhs.p->alertRadarSite_ &&
           lhs.p->alertRadius_ == rhs.p->alertRadius_ &&
           lhs.p->alertCounty_ == rhs.p->alertCounty_ &&
           lhs.p->alertWFO_ == rhs.p->alertWFO_ &&
           lhs.p->alertEnabled_ == rhs.p->alertEnabled_ &&
           lhs.p->alertSoundFiles_ == rhs.p->alertSoundFiles_ &&
           lhs.p->ignoreMissingCodecs_ == rhs.p->ignoreMissingCodecs_ &&
           lhs.p->alertOnlyNew_ == rhs.p->alertOnlyNew_ &&
           lhs.p->masterVolume_ == rhs.p->masterVolume_);
}

} // namespace scwx::qt::settings
