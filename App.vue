<script>
import { mapGetters, mapActions } from "vuex";
import IMSDK, {
  IMMethods,
  MessageReceiveOptType,
  MessageType,
  SessionType,
} from "openim-uniapp-polyfill";
import config from "./common/config";
import { getDbDir, toastWithCallback } from "@/util/common.js";
import { conversationSort } from "@/util/imCommon";
import { PageEvents, UpdateMessageTypes } from "@/constant";
import { checkUpdateFormPgyer } from "@/api/checkUpdate";
import NotificationUtil from "./util/notification";

let cacheConversationList = [];
let updateDownloadTask = null;
let notificationIntance = null;
let pausing = false;

export default {
  onLaunch: function () {
    console.log("App Launch");
    // Igexin.turnOnPush();
    this.launchCheck();
    this.setGlobalIMlistener();
    this.setPageListener();
    this.tryLogin();
    console.warn(`建议开发前先查看文档（https://docs.openim.io/zh-Hans/sdks/quickstart/uniapp）。必须先按照文档配置，否则无法运行！`);
    // #ifdef H5 || MP-WEIXIN
    console.warn(`该客户端目前是3.8.2版本，使用了 JSSDK 直接连接到 im-server，请确保im-server版本大于 3.8.2`);
    // #endif
  },
  onShow: function () {
    console.log("App Show");
    // #ifdef APP-PLUS
    IMSDK.asyncApi(IMSDK.IMMethods.SetAppBackgroundStatus, IMSDK.uuid(), false);
    // #endif
  },
  onHide: function () {
    console.log("App Hide");
    // #ifdef APP-PLUS
    IMSDK.asyncApi(IMSDK.IMMethods.SetAppBackgroundStatus, IMSDK.uuid(), true);
    // #endif
  },
  computed: {
    ...mapGetters([
      "storeConversationList",
      "storeCurrentConversation",
      "storeCurrentUserID",
      "storeSelfInfo",
      "storeRecvFriendApplications",
      "storeRecvGroupApplications",
      "storeHistoryMessageList",
      "storeIsSyncing",
    ]),
    contactBadgeRely() {
      return {
        recvFriendApplications: this.storeRecvFriendApplications,
        recvGroupApplications: this.storeRecvGroupApplications,
        userKey: this.storeCurrentUserID,
      };
    },
  },
  methods: {
    ...mapActions("message", [
      "pushNewMessage",
      "updateOneMessage",
      "updateQuoteMessageRevoke",
      "updateMessageNicknameAndFaceUrl",
    ]),
    ...mapActions("conversation", ["updateCurrentMemberInGroup"]),
    ...mapActions("contact", [
      "updateFriendInfo",
      "pushNewFriend",
      "updateBlackInfo",
      "pushNewBlack",
      "pushNewGroup",
      "updateGroupInfo",
      "pushNewRecvFriendApplition",
      "updateRecvFriendApplition",
      "pushNewSentFriendApplition",
      "updateSentFriendApplition",
      "pushNewRecvGroupApplition",
      "updateRecvGroupApplition",
      "pushNewSentGroupApplition",
      "updateSentGroupApplition",
    ]),
    setGlobalIMlistener() {
      console.log("setGlobalIMlistener");
      // init
      const kickHander = (message) => {
        toastWithCallback(message, () => {
          uni.removeStorage({
            key: "IMToken",
          });
          uni.removeStorage({
            key: "BusinessToken",
          });
          // Igexin.unbindAlias(this.storeCurrentUserID)
          uni.$u.route("/pages/login/index");
        });
      };
      IMSDK.subscribe(IMSDK.IMEvents.OnConnectFailed, ({ errCode }) => {
        console.log('OnConnectFailed', errCode)
      });
      IMSDK.subscribe(IMSDK.IMEvents.OnConnecting, (data) => {
        console.log('OnConnecting', data)
      });
      IMSDK.subscribe(IMSDK.IMEvents.OnConnectSuccess, (data) => {
        console.log('OnConnectSuccess', data)
      });
      IMSDK.subscribe(IMSDK.IMEvents.OnKickedOffline, (data) => {
        console.log('OnKickedOffline', data)
        kickHander("您的账号在其他设备登录，请重新登陆！");
      });
      IMSDK.subscribe(IMSDK.IMEvents.OnUserTokenExpired, (data) => {
        console.log('OnUserTokenExpired', data)
        kickHander("您的登录已过期，请重新登陆！");
      });
      IMSDK.subscribe('onUserTokenInvalid', (data) => {
        kickHander("您的登录已无效，请重新登陆！");
      });

      // sync
      const syncStartHandler = () => {
        uni.showLoading({
          title: "同步中",
          mask: true,
        });
        this.$store.commit("user/SET_IS_SYNCING", true);
      };
      const syncFinishHandler = () => {
        uni.hideLoading();
        this.$store.dispatch("conversation/getConversationList");
        this.$store.dispatch("contact/getFriendList");
        this.$store.dispatch("contact/getGrouplist");
        this.$store.dispatch("conversation/getUnReadCount");
        this.$store.commit("user/SET_IS_SYNCING", false);
      };
      const syncFailedHandler = () => {
        uni.hideLoading();
        uni.$u.toast("同步消息失败");
        this.$store.dispatch("conversation/getConversationList");
        this.$store.dispatch("conversation/getUnReadCount");
        this.$store.commit("user/SET_IS_SYNCING", false);
      };
      IMSDK.subscribe(IMSDK.IMEvents.OnSyncServerStart, syncStartHandler);
      IMSDK.subscribe(IMSDK.IMEvents.OnSyncServerFinish, syncFinishHandler);
      IMSDK.subscribe(IMSDK.IMEvents.OnSyncServerFailed, syncFailedHandler);

      // self
      const selfInfoUpdateHandler = ({ data }) => {
        this.$store.commit("user/SET_SELF_INFO", {
          ...this.storeSelfInfo,
          ...data,
        });
      };

      IMSDK.subscribe(IMSDK.IMEvents.OnSelfInfoUpdated, selfInfoUpdateHandler);

      // message
      const newMessagesHandler = ({ data }) => {
        if (this.storeIsSyncing) {
          return;
        }
        data.forEach(this.handleNewMessage);
      };
      const c2cReadReceiptHandler = ({ data: receiptList }) => {
        if (receiptList[0].userID !== this.storeCurrentConversation.userID) {
          return;
        }

        receiptList.forEach((item) => {
          item.msgIDList.forEach((msgID) => {
            this.updateOneMessage({
              message: {
                clientMsgID: msgID,
              },
              type: UpdateMessageTypes.KeyWords,
              keyWords: {
                key: "isRead",
                value: true,
              },
            });
          });
        });
      };
      const groupReadReceiptHandler = ({ data: receiptList }) => {
        if (receiptList[0].groupID !== this.storeCurrentConversation.groupID) {
          return;
        }

        receiptList.forEach((item) => {
          item.msgIDList.forEach((msgID) => {
            const inlineMessage = this.storeHistoryMessageList.find(
              (message) => message.clientMsgID === msgID,
            );
            if (inlineMessage) {
              inlineMessage.attachedInfoElem.groupHasReadInfo.hasReadUserIDList =
                [
                  ...(inlineMessage.attachedInfoElem.groupHasReadInfo
                    .hasReadUserIDList ?? []),
                  item.userID,
                ];
              // Members who join later in the workgroup will also send read receipts. Need filter.
              if (
                inlineMessage.attachedInfoElem.groupHasReadInfo
                  .groupMemberCount -
                  inlineMessage.attachedInfoElem.groupHasReadInfo.hasReadCount >
                1
              ) {
                inlineMessage.attachedInfoElem.groupHasReadInfo.hasReadCount += 1;
              }

              console.log({
                ...inlineMessage,
              });
              this.updateOneMessage({
                message: {
                  ...inlineMessage,
                },
              });
            }
          });
        });
      };
      const newRecvMessageRevokedHandler = ({ data: revokedMessage }) => {
        if (!this.storeCurrentConversation.conversationID) {
          return;
        }

        this.updateOneMessage({
          message: {
            clientMsgID: revokedMessage.clientMsgID,
          },
          type: UpdateMessageTypes.KeyWords,
          keyWords: [
            {
              key: "contentType",
              value: MessageType.RevokeMessage,
            },
            {
              key: "notificationElem",
              value: {
                detail: JSON.stringify(revokedMessage),
              },
            },
          ],
        });
        this.updateQuoteMessageRevoke({
          clientMsgID: revokedMessage.clientMsgID
        })
      };

      IMSDK.subscribe(IMSDK.IMEvents.OnRecvNewMessages, newMessagesHandler);
      IMSDK.subscribe(
        IMSDK.IMEvents.OnRecvC2CReadReceipt,
        c2cReadReceiptHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnRecvGroupReadReceipt,
        groupReadReceiptHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnNewRecvMessageRevoked,
        newRecvMessageRevokedHandler,
      );

      // friend
      const friendInfoChangeHandler = ({ data }) => {
        console.log(data);
        if (data.userID === this.storeCurrentConversation?.userID) {
          this.updateMessageNicknameAndFaceUrl({
            sendID: data.userID,
            senderNickname: data.nickname,
            senderFaceUrl: data.faceURL,
          });
          this.$store.commit(
            "conversation/SET_CURRENT_CONVERSATION",
            {...this.storeCurrentConversation, showName: data.nickname},
          );
        }
        console.log(this.storeConversationList)
        this.updateFriendInfo({
          friendInfo: data,
        });
      };
      const friendAddedHandler = ({ data }) => {
        this.pushNewFriend(data);
      };
      const friendDeletedHander = ({ data }) => {
        this.updateFriendInfo({
          friendInfo: data,
          isRemove: true,
        });
      };

      IMSDK.subscribe(
        IMSDK.IMEvents.OnFriendInfoChanged,
        friendInfoChangeHandler,
      );
      IMSDK.subscribe(IMSDK.IMEvents.OnFriendAdded, friendAddedHandler);
      IMSDK.subscribe(IMSDK.IMEvents.OnFriendDeleted, friendDeletedHander);

      // blacklist
      const blackAddedHandler = ({ data }) => {
        this.pushNewBlack(data);
      };
      const blackDeletedHandler = ({ data }) => {
        this.updateBlackInfo({
          blackInfo: data,
          isRemove: true,
        });
      };

      IMSDK.subscribe(IMSDK.IMEvents.OnBlackAdded, blackAddedHandler);
      IMSDK.subscribe(IMSDK.IMEvents.OnBlackDeleted, blackDeletedHandler);

      // group
      const joinedGroupAddedHandler = ({ data }) => {
        this.pushNewGroup(data);
      };
      const joinedGroupDeletedHandler = ({ data }) => {
        this.updateGroupInfo({
          groupInfo: data,
          isRemove: true,
        });
      };
      const groupInfoChangedHandler = ({ data }) => {
        this.updateGroupInfo({
          groupInfo: data,
        });
      };
      const groupMemberInfoChangedHandler = ({ data }) => {
        if (data.groupID === this.storeCurrentConversation?.groupID) {
          this.updateMessageNicknameAndFaceUrl({
            sendID: data.userID,
            senderNickname: data.nickname,
            senderFaceUrl: data.faceURL,
          });
          this.updateCurrentMemberInGroup(data);
        }
      };

      IMSDK.subscribe(
        IMSDK.IMEvents.OnJoinedGroupAdded,
        joinedGroupAddedHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnJoinedGroupDeleted,
        joinedGroupDeletedHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnGroupInfoChanged,
        groupInfoChangedHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnGroupMemberInfoChanged,
        groupMemberInfoChangedHandler,
      );

      // application
      const friendApplicationNumHandler = ({ data }) => {
        const isRecv = data.toUserID === this.storeCurrentUserID;
        if (isRecv) {
          this.pushNewRecvFriendApplition(data);
        } else {
          this.pushNewSentFriendApplition(data);
        }
      };
      const friendApplicationAccessHandler = ({ data }) => {
        const isRecv = data.toUserID === this.storeCurrentUserID;
        if (isRecv) {
          this.updateRecvFriendApplition({
            application: data,
          });
        } else {
          this.updateSentFriendApplition({
            application: data,
          });
        }
      };
      const groupApplicationNumHandler = ({ data }) => {
        const isRecv = data.userID !== this.storeCurrentUserID;
        if (isRecv) {
          this.pushNewRecvGroupApplition(data);
        } else {
          this.pushNewSentGroupApplition(data);
        }
      };
      const groupApplicationAccessHandler = ({ data }) => {
        const isRecv = data.userID !== this.storeCurrentUserID;
        if (isRecv) {
          this.updateRecvGroupApplition({
            application: data,
          });
        } else {
          this.updateSentGroupApplition({
            application: data,
          });
        }
      };

      IMSDK.subscribe(
        IMSDK.IMEvents.OnFriendApplicationAdded,
        friendApplicationNumHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnFriendApplicationAccepted,
        friendApplicationAccessHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnFriendApplicationRejected,
        friendApplicationAccessHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnGroupApplicationAdded,
        groupApplicationNumHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnGroupApplicationAccepted,
        groupApplicationAccessHandler,
      );
      IMSDK.subscribe(
        IMSDK.IMEvents.OnGroupApplicationRejected,
        groupApplicationAccessHandler,
      );

      // conversation
      const totalUnreadCountChangedHandler = ({ data }) => {
        if (this.storeIsSyncing) {
          return;
        }
        this.$store.commit("conversation/SET_UNREAD_COUNT", data);
      };
      const newConversationHandler = ({ data }) => {
        if (this.storeIsSyncing) {
          return;
        }
        const result = [...data, ...this.storeConversationList];
        this.$store.commit(
          "conversation/SET_CONVERSATION_LIST",
          conversationSort(result),
        );
      };
      const conversationChangedHandler = ({ data }) => {
        if (this.storeIsSyncing) {
          return;
        }
        let filterArr = [];
        console.log(data);
        const chids = data.map((ch) => ch.conversationID);
        filterArr = this.storeConversationList.filter(
          (tc) => !chids.includes(tc.conversationID),
        );
        const idx = data.findIndex(
          (c) =>
            c.conversationID === this.storeCurrentConversation.conversationID,
        );
        if (idx !== -1)
          this.$store.commit(
            "conversation/SET_CURRENT_CONVERSATION",
            data[idx],
          );
        const result = [...data, ...filterArr];
        this.$store.commit(
          "conversation/SET_CONVERSATION_LIST",
          conversationSort(result),
        );
      };

      IMSDK.subscribe(
        IMSDK.IMEvents.OnTotalUnreadMessageCountChanged,
        totalUnreadCountChangedHandler,
      );
      IMSDK.subscribe(IMSDK.IMEvents.OnNewConversation, newConversationHandler);
      IMSDK.subscribe(
        IMSDK.IMEvents.OnConversationChanged,
        conversationChangedHandler,
      );
    },

    setPageListener() {
      uni.$on(PageEvents.CheckForUpdate, this.checkVersion);
    },
    tryLogin() {
      const initStore = () => {
        this.$store.dispatch("user/getSelfInfo");
        this.$store.dispatch("conversation/getConversationList");
        this.$store.dispatch("conversation/getUnReadCount");
        // this.$store.dispatch("contact/getFriendList");
        // this.$store.dispatch("contact/getGrouplist");
        this.$store.dispatch("contact/getBlacklist");
        this.$store.dispatch("contact/getRecvFriendApplications");
        this.$store.dispatch("contact/getSentFriendApplications");
        this.$store.dispatch("contact/getRecvGroupApplications");
        this.$store.dispatch("contact/getSentGroupApplications");
        uni.switchTab({
          url: "/pages/conversation/conversationList/index?isRedirect=true",
        });
      };
      let platformID;
      // #ifdef H5
      platformID = 5
      // #endif
      // #ifdef MP-WEIXIN
      platformID = 6
      // #endif
			// #ifdef H5 || MP-WEIXIN
			const IMToken = uni.getStorageSync("IMToken");
			console.log("🚀 ~ h5 tryLogin ~ IMToken:", IMToken)
			const IMUserID = uni.getStorageSync("IMUserID");
			 console.log("🚀 ~ h5 tryLogin ~ IMUserID:", IMUserID)
			 if (IMToken && IMUserID) {
				IMSDK.asyncApi(IMSDK.IMMethods.Login, IMSDK.uuid(), {
					userID: IMUserID,
					token: IMToken,
					platformID,
					wsAddr: config.getWsUrl(),
					apiAddr: config.getApiUrl(),
				})
					.then((res) => {
						initStore()
						console.log("success", res);
					})
					.catch((err) => {
						console.log("error", err);
						uni.removeStorage({
							key: "IMToken",
						});
						uni.removeStorage({
							key: "BusinessToken",
						});
					});
			}
			// #endif
			// #ifdef APP-PLUS
      getDbDir()
        .then(async (path) => {
          console.log("🚀 ~ getDbDir.then ~ path:", path)
          const flag = await IMSDK.asyncApi(IMMethods.InitSDK, IMSDK.uuid(), {
            platformID: uni.$u.os() === "ios" ? 1 : 2,
            apiAddr: config.getApiUrl(),
            wsAddr: config.getWsUrl(),
            dataDir: path, // 数据存储路径
            logLevel: 6,
            logFilePath: path,
            isLogStandardOutput: true,
            isExternalExtensions: false,
          });
          console.log("app.vue ~ flag ~ flag:", flag)
          if (!flag) {
            plus.navigator.closeSplashscreen();
            uni.$u.toast("初始化IMSDK失败！");
            return;
          }
          const status = await IMSDK.asyncApi(
            IMSDK.IMMethods.GetLoginStatus,
            IMSDK.uuid(),
          );
          console.log("🚀 ~ .then ~ status:", status)
          if (status === 3) {
            initStore();
            return;
          }

          const IMToken = uni.getStorageSync("IMToken");
          console.log("🚀 ~ .then ~ IMToken:", IMToken)
          const IMUserID = uni.getStorageSync("IMUserID");
          console.log("🚀 ~ .then ~ IMUserID:", IMUserID)
          if (IMToken && IMUserID) {
            IMSDK.asyncApi(IMSDK.IMMethods.Login, IMSDK.uuid(), {
              userID: IMUserID,
              token: IMToken,
            })
              .then(initStore)
              .catch((err) => {
                console.log(err);
                uni.removeStorage({
                  key: "IMToken",
                });
                uni.removeStorage({
                  key: "BusinessToken",
                });
                plus.navigator.closeSplashscreen();
              });
          } else {
            plus.navigator.closeSplashscreen();
          }
        })
        .catch((err) => {
          console.log("get dir failed", err);
          plus.navigator.closeSplashscreen();
        });
				// #endif
    },

    launchCheck() {
      // #ifdef APP-PLUS
      plus.globalEvent.addEventListener("newintent", (e) => {
        console.log(plus.runtime.arguments);
        let launchData = {};
        try {
          launchData = JSON.parse(plus.runtime.arguments);
        } catch (e) {}
        switch (launchData.type) {
          case "updateDownloadFinish":
            break;
          case "updateProgress":
            if (pausing) return;

            if (updateDownloadTask?.state === 5) {
              updateDownloadTask?.resume();
            } else {
              updateDownloadTask?.pause();
              this.updateNotification("暂停中");
              pausing = true;
            }
            break;
          default:
            break;
        }
      });
      // #endif
    },
    checkVersion(initiative = false) {
      if (uni.$u.os() === "ios") {
        return;
      }
      // #ifdef APP-PLUS
      plus.runtime.getProperty(plus.runtime.appid, ({ version }) => {
        checkUpdateFormPgyer(version)
          .then(
            async ({
              buildHaveNewVersion,
              needForceUpdate,
              downloadURL,
              buildUpdateDescription,
              buildVersion,
            }) => {
              if (buildHaveNewVersion) {
                const hasDownloadedPath =
                  await this.checkDownloadedPkg(buildVersion);
                uni.showModal({
                  title: "检测到新版本",
                  content: hasDownloadedPath
                    ? "新版本已下载完毕，是否立即更新？"
                    : buildUpdateDescription || "",
                  showCancel: !needForceUpdate,
                  confirmText: hasDownloadedPath ? "立即更新" : "立即升级",
                  cancelText: "下次再说",
                  success: (res) => {
                    if (res.confirm) {
                      if (hasDownloadedPath) {
                        plus.runtime.install(hasDownloadedPath);
                      } else {
                        this.downloadApk(downloadURL, buildVersion);
                      }
                    } else {
                      if (needForceUpdate) {
                        plus.runtime.quit();
                      }
                    }
                  },
                });
              }

              if (initiative && !buildHaveNewVersion) {
                uni.$u.toast("当前已是最新版本！");
              }
            },
          )
          .catch((err) => {
            console.log(err);
            if (initiative) {
              uni.$u.toast("获取版本信息失败！");
            }
          })
          .finally(
            () => initiative && uni.$emit(PageEvents.CheckForUpdateResp),
          );
      });
      // #endif
    },
    async checkDownloadedPkg(buildVersion) {
      const versionMap = uni.getStorageSync("IMVersionMap") || {};
      const filePath = versionMap[buildVersion];
      const [err] = await uni.getSavedFileInfo({
        filePath,
      });
      return err ? "" : filePath;
    },
    downloadApk(downloadURL, buildVersion) {
      notificationIntance = new NotificationUtil();
      updateDownloadTask = plus.downloader.createDownload(downloadURL);

      updateDownloadTask.addEventListener("statechanged", (task, status) => {
        if (task.state === 3) {
          uni.$u.throttle(() => this.updateNotification("正在下载..."), 1000);
        }

        if (task.state === 5) {
          this.updateNotification("已暂停", true);
          pausing = false;
        }

        if (task.state === 4) {
          if (status === 200) {
            this.updateNotification("正在下载...");
            setTimeout(() => this.downloadFinish(buildVersion), 200);
          }
        }
      });
      updateDownloadTask.start();
    },
    updateNotification(content, isPause = false) {
      const progress = parseInt(
        (updateDownloadTask.downloadedSize / updateDownloadTask.totalSize) *
          100,
      );
      const config = {
        progress,
        isPause,
        title: "OpenIM",
        content: `${content}${progress}%`,
        intentList: [["type", "updateProgress"]],
      };
      notificationIntance.createProgress(config);
    },
    downloadFinish(buildVersion) {
      notificationIntance.clearNotification(1000);
      notificationIntance.compProgressNotification({
        title: "OpenIM",
        content: "下载完成",
        notifyId: 1001,
      });
      uni.showModal({
        title: "准备更新",
        content: "安装包已下载完毕，是否立即更新？",
        showCancel: false,
        confirmText: "立即更新",
        success: (res) => {
          const pkgPath = plus.io.convertLocalFileSystemURL(
            updateDownloadTask.filename,
          );
          if (res.confirm) {
            plus.runtime.install(pkgPath);
          } else {
            const versionMap = uni.getStorageSync("IMVersionMap") || {};
            versionMap[buildVersion] = pkgPath;
            uni.setStorage({
              key: "IMVersionMap",
              data: versionMap,
            });
          }
        },
      });
    },

    async newMessageNotify(newServerMsg) {
      if (this.storeIsSyncing) {
        return;
      }

      const disableNotify = uni.getStorageSync(
        `${this.storeCurrentUserID}_DisableNotify`,
      );
      if (
        disableNotify ||
        this.storeSelfInfo.globalRecvMsgOpt !== MessageReceiveOptType.Nomal
      ) {
        return;
      }

      let cveItem = [
        ...this.storeConversationList,
        ...cacheConversationList,
      ].find((conversation) => {
        if (newServerMsg.sessionType === SessionType.WorkingGroup) {
          return newServerMsg.groupID === conversation.groupID;
        }
        return newServerMsg.sendID === conversation.userID;
      });

      if (!cveItem) {
        try {
          const { data } = await IMSDK.asyncApi(
            IMSDK.IMMethods.GetOneConversation,
            IMSDK.uuid(),
            {
              sourceID: newServerMsg.groupID || newServerMsg.sendID,
              sessionType: newServerMsg.sessionType,
            },
          );
          cveItem = data;
          cacheConversationList = [...cacheConversationList, data];
        } catch (e) {
          return;
        }
      }

      if (cveItem.recvMsgOpt !== MessageReceiveOptType.Nomal) {
        return;
      }

      // uni.createPushMessage({
      // 	content: `${newServerMsg.senderNickname}: ${parseMessageByType(newServerMsg)}`,
      // 	payload: {
      // 		sessionType: newServerMsg.sessionType,
      // 		sourceID: newServerMsg.groupID || newServerMsg.sendID,
      // 	}
      // })
    },
    handleNewMessage(newServerMsg) {
      if (this.inCurrentConversation(newServerMsg)) {
        const isSingleMessage = newServerMsg.sessionType === SessionType.Single;

        if (isSingleMessage) {
          uni.$u.throttle(() => uni.$emit(PageEvents.OnlineStateCheck), 2000);
        }

        if (newServerMsg.contentType === MessageType.TypingMessage) {
          if (isSingleMessage) {
            uni.$emit(PageEvents.TypingUpdate);
          }
        } else {
          if (newServerMsg.contentType === MessageType.RevokeMessage) {
          } else {
            newServerMsg.isAppend = true;
            this.pushNewMessage(newServerMsg);
            setTimeout(() => uni.$emit(PageEvents.ScrollToBottom, true));
          }
          uni.$u.debounce(this.markConversationAsRead, 2000);
        }
      } else {
        if (newServerMsg.contentType !== MessageType.TypingMessage) {
          uni.$u.throttle(() => this.newMessageNotify(newServerMsg), 1000);
        }
      }
    },
    inCurrentConversation(newServerMsg) {
      switch (newServerMsg.sessionType) {
        case SessionType.Single:
          return (
            newServerMsg.sendID === this.storeCurrentConversation.userID ||
            (newServerMsg.sendID === this.storeCurrentUserID &&
              newServerMsg.recvID === this.storeCurrentConversation.userID)
          );
        case SessionType.WorkingGroup:
          return newServerMsg.groupID === this.storeCurrentConversation.groupID;
        case SessionType.Notification:
          return newServerMsg.sendID === this.storeCurrentConversation.userID;
        default:
          return false;
      }
    },
    markConversationAsRead() {
      IMSDK.asyncApi(
        IMSDK.IMMethods.MarkConversationMessageAsRead,
        IMSDK.uuid(),
        this.storeCurrentConversation.conversationID,
      );
    },
  },
  watch: {
    storeCurrentUserID(newVal) {
      if (newVal) {
        cacheConversationList = [];
      }
    },
    contactBadgeRely: {
      handler(newValue) {
        const { recvFriendApplications, recvGroupApplications, userKey } =
          newValue;
        if (!userKey) return;
        const accessedFriendApplications =
          uni.getStorageSync(`${userKey}_accessedFriendApplications`) || [];
        let unHandleFriendApplicationNum = recvFriendApplications.filter(
          (application) =>
            application.handleResult === 0 &&
            !accessedFriendApplications.includes(
              `${application.fromUserID}_${application.createTime}`,
            ),
        ).length;
        const accessedGroupApplications =
          uni.getStorageSync(`${userKey}_accessedGroupApplications`) || [];
        let unHandleGroupApplicationNum = recvGroupApplications.filter(
          (application) =>
            application.handleResult === 0 &&
            !accessedGroupApplications.includes(
              `${application.userID}_${application.createTime}`,
            ),
        ).length;
        const total =
          unHandleFriendApplicationNum + unHandleGroupApplicationNum;
        if (total) {
          uni.setTabBarBadge({
            index: 1,
            text: total < 99 ? total + "" : "99+",
          });
        } else {
          uni.removeTabBarBadge({
            index: 1,
          });
        }
        this.$store.commit(
          "contact/SET_UNHANDLE_FRIEND_APPLICATION_NUM",
          unHandleFriendApplicationNum,
        );
        this.$store.commit(
          "contact/SET_UNHANDLE_GROUP_APPLICATION_NUM",
          unHandleGroupApplicationNum,
        );
      },
      deep: true,
    },
  },
};
</script>

<style lang="scss">
/*每个页面公共css */
@import "@/uni_modules/uview-ui/index.scss";
@import "@/styles/login.scss";
@import "@/styles/global.scss";

uni-page-body {
  height: 100vh;
  overflow: hidden;
}

.uni-tabbar .uni-tabbar__icon {
  width: 28px !important;
  height: 28px !important;
}
</style>
