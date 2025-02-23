#include <rclcpp/rclcpp.hpp>
#include <tier4_vehicle_msgs/msg/actuation_command_stamped.hpp>
#include <autoware_auto_vehicle_msgs/msg/gear_report.hpp>

class RawVehicleCmdSelectPublisher
{
private://saved data
	int gear_num_;//担当gearナンバー　autoware_auto_vehicle_msgs::GearReportのDRIVE定数がはいる

public://RawVehicleCmdSelectorから設定
	static uint8_t gear_report_;//現在のギア情報

private://ros node
	rclcpp::Node *node_;

private://ros publisher
	rclcpp::Publisher<tier4_vehicle_msgs::msg::ActuationCommandStamped>::SharedPtr pub_cmd_;//現在のgearが担当のgearと同じ場合、subscibeしたtopicをpublishする RawVehicleCmdSelectorで同じインスタンスを渡す

private://ros subscriber
	rclcpp::Subscription<tier4_vehicle_msgs::msg::ActuationCommandStamped>::SharedPtr sub_cmd_;//各GEARのraw_vehicle_converterからのコマンドtopic

public://init
	RawVehicleCmdSelectPublisher(rclcpp::Node *node, const std::string &topic_name, const int gear_num,
		const rclcpp::Publisher<tier4_vehicle_msgs::msg::ActuationCommandStamped>::SharedPtr pub)
		: gear_num_(gear_num)
		, node_(node)
		, pub_cmd_(pub)
		, sub_cmd_(node_->create_subscription<tier4_vehicle_msgs::msg::ActuationCommandStamped>(topic_name, rclcpp::QoS(1),
			std::bind(&RawVehicleCmdSelectPublisher::callbackCmd, this, std::placeholders::_1)))
	{
	}

private:
	void callbackCmd(const tier4_vehicle_msgs::msg::ActuationCommandStamped::ConstSharedPtr cmd)
	{
		if(gear_report_ == gear_num_)
		{
			pub_cmd_->publish(*cmd);
		}
	}
};

uint8_t RawVehicleCmdSelectPublisher::gear_report_ = autoware_auto_vehicle_msgs::msg::GearReport::DRIVE;;


class RawVehicleCmdSelector : public rclcpp::Node
{
private://ros subscriber
	rclcpp::Subscription<autoware_auto_vehicle_msgs::msg::GearReport>::SharedPtr sub_gear_report_;//車両gear情報
	std::vector<std::shared_ptr<RawVehicleCmdSelectPublisher>> publishers_;//各GEARのraw_vehicle_converterからのコマンドtopic

public://init
	RawVehicleCmdSelector(const rclcpp::NodeOptions &options) noexcept(false)
		: rclcpp::Node("raw_vehicle_cmd_selector", options)
		, publishers_(createPublishers())
	{
	}

private://init
	//各GEAR D のraw_vehicle_converterからのコマンドtopicのsubscriberを作成
	std::vector<std::shared_ptr<RawVehicleCmdSelectPublisher>> createPublishers()
	{
		rclcpp::Publisher<tier4_vehicle_msgs::msg::ActuationCommandStamped>::SharedPtr pub_cmd_ =
			create_publisher<tier4_vehicle_msgs::msg::ActuationCommandStamped>("actuation_cmd", rclcpp::QoS(1));

		std::vector<std::shared_ptr<RawVehicleCmdSelectPublisher>> pubs;
		try{
			for(uint8_t i=0; i<255; i++)
			{
				std::stringstream ss_gear;
				ss_gear << "gear" << i+1;
				int gear_num = declare_parameter<int>(ss_gear.str(), -1);
				if(gear_num < 0) break;

				std::stringstream ss_cmd;
				ss_cmd << "cmd" << i+1;
				std::string topic_name = declare_parameter<std::string>(ss_cmd.str(), "");
				if(topic_name == "") break;

				pubs.push_back(std::make_shared<RawVehicleCmdSelectPublisher>(this, topic_name, gear_num, pub_cmd_));
				RCLCPP_INFO_STREAM(get_logger(), "topic_name is " << topic_name);
			}
		}
		catch(const std::runtime_error &e){ throw(e); }

		return pubs;
	}

private://ros subscriber
	//車両gear情報
	void callbackGearReport(const autoware_auto_vehicle_msgs::msg::GearReport::ConstSharedPtr report)
	{
		RawVehicleCmdSelectPublisher::gear_report_ = report->report;
	}
};

int main(int argc, char** argv)
{
	rclcpp::init(argc, argv);
	rclcpp::NodeOptions options;
	std::shared_ptr<RawVehicleCmdSelector> node;
	try
	{
		node = std::make_shared<RawVehicleCmdSelector>(options);
	}
	catch(const std::exception& e)
	{
		RCLCPP_ERROR_STREAM(rclcpp::get_logger("raw_vehicle_cmd_selector"), e.what());
		return 0;
	}

	rclcpp::spin(node);
	rclcpp::shutdown();
	return 0;
}