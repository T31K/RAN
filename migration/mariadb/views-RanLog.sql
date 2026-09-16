USE RanLog;
DROP VIEW IF EXISTS viewLogHackProgram;
CREATE VIEW viewLogHackProgram AS SELECT   LogHackProgram.HackNum, 
                LogHackProgram.UserNum, LogHackProgram.ChaNum, 
                LogHackProgram.SGNum, LogHackProgram.SvrNum, 
                LogHackProgram.HackProgramNum, LogHackProgram.HackDate, 
                LogHackProgram.HackComment, 
                HackProgramList.HackProgramName
FROM      LogHackProgram LEFT OUTER JOIN
                HackProgramList ON 
                LogHackProgram.HackProgramNum = HackProgramList.HackProgramNum;
DROP VIEW IF EXISTS viewLogItemExchangeMax;
CREATE VIEW viewLogItemExchangeMax AS SELECT   MAX(MakeNum) AS MaxNum, NIDMain, NIDSub, MakeType, 
                SGNum, SvrNum, FldNum
FROM      LogItemExchange
GROUP BY SGNum, SvrNum, FldNum, MakeType, NIDMain, NIDSub
ORDER BY SGNum, SvrNum, FldNum, MakeType, NIDMain, NIDSub, MaxNum;
DROP VIEW IF EXISTS viewLogItemMax;
CREATE VIEW viewLogItemMax AS SELECT LogItemMax.NIDMain, LogItemMax.NIDSub, 
       LogItemMax.SGNum, LogItemMax.SvrNum, LogItemMax.FldNum, 
       LogItemMax.MakeType, LogItemMax.MaxNum, 
       ItemList.ItemName
FROM   LogItemMax 
       INNER JOIN ItemList ON 
       ItemList.ItemMain = LogItemMax.NIDMain AND ItemList.ItemSub = LogItemMax.NIDSub
ORDER BY LogItemMax.MaxNum DESC;
DROP VIEW IF EXISTS viewShopPurchase;
CREATE VIEW viewShopPurchase AS SELECT   ShopPurchase.PurKey, ShopPurchase.UserUID, 
                ShopPurchase.ProductNum, ShopPurchase.PurPrice, 
                ShopPurchase.PurFlag, ShopPurchase.PurDate, 
                ShopPurchase.PurChgDate, ShopItemMap.ItemMain, 
                ShopItemMap.ItemSub, ShopItemMap.ItemName, 
                ShopPurFlag.PurFlagName
FROM      ShopPurchase INNER JOIN
                ShopItemMap ON 
                ShopPurchase.ProductNum = ShopItemMap.ProductNum LEFT OUTER JOIN
                ShopPurFlag ON ShopPurchase.PurFlag = ShopPurFlag.PurFlag;
DROP VIEW IF EXISTS viewShopPurchaseItem;
CREATE VIEW viewShopPurchaseItem AS SELECT   SUM(A.PurPrice) AS tPrice, A.ItemMain, A.ItemSub, 
                B.ItemName
FROM      viewShopPurchase A INNER JOIN
                ShopItemMap B ON A.ProductNum = B.ProductNum
GROUP BY A.ItemMain, A.ItemSub, B.ItemName
ORDER BY A.ItemMain, A.ItemSub;
