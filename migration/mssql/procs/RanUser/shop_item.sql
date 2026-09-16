create proc [dbo].[shop_item]
	@userid varchar(20),
	@pronum bigint
as
	begin
	        DECLARE @ID BIGINT
                SELECT @ID = COUNT(PurKey) FROM shopPurchase
                IF(@ID IS NULL)
                        SET @ID = 0
                AATT:
                SET @ID = @ID + 1
                IF EXISTS (SELECT PurKey FROM shopPurchase  WHERE PurKey=@ID)
                BEGIN
                        GoTo AATT
                End
		Insert into shopPurchase(PurKey,userUid,productNum) values(@ID,@userid,@pronum)                
                SELECT '1'
	end

