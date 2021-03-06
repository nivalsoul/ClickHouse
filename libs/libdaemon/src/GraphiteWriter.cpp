#include <daemon/GraphiteWriter.h>
#include <daemon/BaseDaemon.h>
#include <Poco/Util/LayeredConfiguration.h>
#include <Poco/Util/Application.h>
#include <Poco/Net/DNS.h>

#include <mutex>
#include <iomanip>


GraphiteWriter::GraphiteWriter(const std::string & config_name, const std::string & sub_path)
{
	Poco::Util::LayeredConfiguration & config = Poco::Util::Application::instance().config();
	port = config.getInt(config_name + ".port", 42000);
	host = config.getString(config_name + ".host", "127.0.0.1");
	timeout = config.getDouble(config_name + ".timeout", 0.1);

	root_path = config.getString(config_name + ".root_path", "one_min");
	if (root_path.size())
		root_path += ".";

	/** Что использовать в качестве имени сервера в названии метрики?
	  *
	  * По-умолчанию (для совместимости с существовавшим ранее поведением),
	  *  в качестве имени сервера берётся строка, аналогичная uname -n,
	  *  а затем к нему приписывается то, что указано в hostname_suffix, если есть.
	  * Часто серверы настроены так, что, например, для сервера example01-01-1.yandex.ru, uname -n будет выдавать example01-01-1
	  * Впрочем, uname -n может быть настроен произвольным образом. Он также может совпадать с FQDN.
	  *
	  * Если указано use_fqdn со значением true,
	  *  то в качестве имени сервера берётся FQDN (uname -f),
	  *  а значение hostname_suffix игнорируется.
	  */
	bool use_fqdn = config.getBool(config_name + ".use_fqdn", false);

	std::string hostname_in_path = use_fqdn
		? Poco::Net::DNS::thisHost().name()	/// То же, что hostname -f
		: Poco::Net::DNS::hostName();		/// То же, что uname -n

	/// Заменяем точки на подчёркивания, чтобы Graphite не интерпретировал их, как разделители пути.
	std::replace(std::begin(hostname_in_path), std::end(hostname_in_path), '.', '_');

	root_path += hostname_in_path;

	if (!use_fqdn)
		root_path += config.getString(config_name + ".hostname_suffix", "");

	if (sub_path.size())
		root_path += "." + sub_path;
}

/// Угадываем имя среды по имени машинки
/// машинки имеют имена вида example01dt.yandex.ru
/// t - test
/// dev - development
/// никакого суффикса - production
std::string getEnvironment()
{
	std::string hostname = Poco::Net::DNS::hostName();
	hostname = hostname.substr(0, hostname.find('.'));

	const std::string development_suffix = "dev";
	if (hostname.back() == 't')
	{
		return "test";
	}
	else if (hostname.size() > development_suffix.size() &&
			hostname.substr(hostname.size() - development_suffix.size()) == development_suffix)
	{
		return "development";
	}
	else
	{
		return "production";
	}
}

std::string GraphiteWriter::getPerLayerPath(
	const std::string & prefix,
	const boost::optional<std::size_t> & layer)
{
	static const std::string environment = getEnvironment();

	std::stringstream path_full;
	path_full << prefix << "." << environment << ".";

	const BaseDaemon & daemon = BaseDaemon::instance();

	const boost::optional<std::size_t> layer_ = layer.is_initialized()
	    ? layer
	    : daemon.getLayer();
	if (layer_)
		path_full << "layer" << std::setfill('0') << std::setw(3) << *layer_ << ".";

	/// Когда несколько демонов запускается на одной машине
	/// к имени демона добавляется цифра.
	/// Удалим последнюю цифру
	std::locale locale;
	std::string command_name = daemon.commandName();
	if (std::isdigit(command_name.back(), locale))
		command_name = command_name.substr(0, command_name.size() - 1);
	path_full << command_name;

	return path_full.str();
}

std::string GraphiteWriter::getPerServerPath(const std::string & server_name, const std::string & root_path)
{
	std::string path = root_path + "." + server_name;
	std::replace(path.begin() + root_path.size() + 1, path.end(), '.', '_');
	return path;
}
